#include "mesh/BinaryGreedyMesher.hpp"

#include "core/Profile.hpp"
#include "mesh/ModelBox.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockShape.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <span>

namespace mc {
namespace {

constexpr i32 kN = kSectionSize; // 32
static_assert(kN == 32, "the occupancy masks below are 32 bits wide");

/// The opacity grid is padded by one voxel on every side, so a neighbour lookup
/// is an array read rather than a branch on which section a coordinate falls in.
///
/// One layer is exactly enough, and that is worth stating because it is not
/// obvious. Face culling only reaches one voxel along the normal. AO reaches one
/// along the normal *and* one along each of the two tangents -- but the tangents
/// are perpendicular to the normal, so those offsets never stack onto the normal's
/// axis. Every sample therefore lands in [-1, 32] on each axis independently.
constexpr i32 kPad = 1;
constexpr i32 kPadded = kN + 2 * kPad; // 34

constexpr usize paddedIndex(i32 x, i32 y, i32 z) {
    MC_ASSERT(x >= -kPad && x < kN + kPad);
    MC_ASSERT(y >= -kPad && y < kN + kPad);
    MC_ASSERT(z >= -kPad && z < kN + kPad);
    // Same axis order as localIndex(): y outermost, x innermost.
    return static_cast<usize>(((y + kPad) * kPadded + (z + kPad)) * kPadded + (x + kPad));
}

constexpr usize kPaddedVolume =
    static_cast<usize>(kPadded) * static_cast<usize>(kPadded) * static_cast<usize>(kPadded);

/// [a][b] -> bitmask over the third axis.
using Mask2D = std::array<std::array<u32, kN>, kN>;
/// [a][b] -> 0 or 1, the occupancy of the voxel just outside the section.
using Boundary2D = std::array<std::array<u8, kN>, kN>;

/// Per-face plane geometry.
///
/// `u` and `v` name the axes the quad extends along; they must match the
/// tangent basis in chunk.vert, where cross(U, V) equals the face normal.
struct FacePlan {
    Face face;
    i32 nx, ny, nz;       ///< outward normal
    i32 ux, uy, uz;       ///< U axis (quad width)
    i32 vx, vy, vz;       ///< V axis (quad height)
    /// The occupancy masks are indexed [a][b]. This says whether the quad's U
    /// axis corresponds to a or to b; V takes the other.
    bool uFromA;
};

constexpr std::array<FacePlan, kFaceCount> kPlans{{
    // face          normal        U            V         uFromA
    {Face::NegX, -1, 0, 0,   0, 0, 1,   0, 1, 0,  false}, // [y][z]
    {Face::PosX, +1, 0, 0,   0, 1, 0,   0, 0, 1,  true},  // [y][z]
    {Face::NegY, 0, -1, 0,   1, 0, 0,   0, 0, 1,  true},  // [x][z]
    {Face::PosY, 0, +1, 0,   0, 0, 1,   1, 0, 0,  false}, // [x][z]
    {Face::NegZ, 0, 0, -1,   0, 1, 0,   1, 0, 0,  false}, // [x][y]
    {Face::PosZ, 0, 0, +1,   1, 0, 0,   0, 1, 0,  true},  // [x][y]
}};

/// Working memory for one meshing call.
///
/// thread_local rather than a parameter: meshing runs on a worker pool where
/// each thread meshes one section at a time, so a per-thread buffer removes
/// ~300 KiB of allocation per section without any ownership plumbing.
struct Scratch {
    /// Materials, centre section only -- a face's material comes from the voxel
    /// behind it, which is always in the centre.
    std::array<BlockId, kSectionVolume> blocks{};

    /// Opacity including the one-voxel shell of neighbours.
    std::array<u8, kPaddedVolume> opaque{};

    /// Fluid occupancy, on the same padded grid. Separate from `opaque` because
    /// water is neither: it hides nothing behind it, and it hides its own faces
    /// from itself.
    ///
    /// **Zero is "not fluid"; anything else is `1 + fluidLevel`.** It was a plain
    /// flag until the surface learned to slope, and storing the level costs nothing:
    /// every existing reader asks `!= 0`, which means the same thing either way, and
    /// the corner-drop pass needs the level of blocks in the shell as much as of
    /// blocks in the centre -- a lake does not stop at a section boundary.
    std::array<u8, kPaddedVolume> fluid{};
    /// Whether the centre section holds any fluid at all. When it does not, the
    /// second pass is skipped whole -- which is almost every section in the world,
    /// so the cost of water existing is one boolean for everything above sea level
    /// and everything under the sea bed.
    bool anyFluid = false;

    /// Cutout occupancy -- glass -- on the same padded grid, and for the same reason
    /// `fluid` is separate: it is neither opaque nor a fluid, and a block that is
    /// none of the three emits nothing at all.
    std::array<u8, kPaddedVolume> cutout{};

    /// Whether the centre section holds any block that is not a full cube.
    ///
    /// **The same gate `anyFluid` and `anyCutout` are, and the same argument.** A
    /// world nobody has built in holds none of these, and the model pass then costs
    /// one boolean rather than a walk over 32,768 voxels per section -- of which a
    /// render distance of 16 meshes about five thousand.
    bool anyModel = false;
    /// Whether the centre section holds any cutout block. Skips the pass whole for
    /// every section nobody has glazed, which is all of them until somebody does --
    /// the same argument `anyFluid` makes, and it matters more here because glass is
    /// player-placed and therefore rare where water is generated and common.
    bool anyCutout = false;

    /// Sky light, on the same padded grid and for the same reason: a face on a
    /// section boundary has to be lit by the light in the neighbour it faces, or
    /// every chunk seam becomes a visible band.
    std::array<u8, kPaddedVolume> light{};

    Mask2D colX{}; ///< [y][z], bit x -- voxels that *emit* a face
    Mask2D colY{}; ///< [x][z], bit y
    Mask2D colZ{}; ///< [x][y], bit z

    /// Voxels that *hide* a face. Equal to col* on the opaque pass, and a superset
    /// on the fluid pass: water is hidden by water and by rock, but only water
    /// emits. Splitting the two is the whole of what makes a body of water render
    /// as a surface rather than as a stack of visible cubes.
    Mask2D cullX{};
    Mask2D cullY{};
    Mask2D cullZ{};

    /// Occupancy of the neighbour voxel immediately below and above each column,
    /// which is what turns "outside is air" into real boundary culling.
    Boundary2D lowX{}, highX{};
    Boundary2D lowY{}, highY{};
    Boundary2D lowZ{}, highZ{};

    std::array<Mask2D, kFaceCount> faceMask{};

    /// Per-plane merge state for the face currently being processed.
    /// planeRows[plane][v] is a bitmask over u; cells[plane][v][u] holds
    /// (layer << 24) | (light << 8) | ao and is only meaningful where the row bit
    /// is set, so it never has to be cleared. Seven bits of layer, sixteen of
    /// light and eight of AO come to 31, which is why one word still holds it.
    std::array<std::array<u32, kN>, kN> planeRows{};
    std::array<std::array<std::array<u32, kN>, kN>, kN> cells{};
};

Scratch& scratch() {
    static thread_local Scratch instance;
    return instance;
}

bool opaqueAt(const Scratch& s, i32 x, i32 y, i32 z) {
    return s.opaque[paddedIndex(x, y, z)] != 0;
}

bool fluidAt(const Scratch& s, i32 x, i32 y, i32 z) {
    return s.fluid[paddedIndex(x, y, z)] != 0;
}

bool cutoutAt(const Scratch& s, i32 x, i32 y, i32 z) {
    return s.cutout[paddedIndex(x, y, z)] != 0;
}

/// Which of the three kinds of geometry a pass is producing.
///
/// **This was a `bool fluidPass` and the third category is why it is not.** Opaque,
/// cutout and fluid differ in exactly two rules -- what emits a face and what hides
/// one -- and a bool could carry two of the three. Glass was not a texture that was
/// missing, it was a block that fell through both branches and emitted nothing.
enum class Pass : u8 {
    /// Rock, dirt, everything solid. Hidden by anything opaque.
    Opaque,
    /// Glass. Emits faces, hides nothing behind it, and is hidden by its own kind so
    /// a wall of glass is a surface rather than a stack of boxes.
    Cutout,
    /// Water. Hidden by rock and by water, and by nothing else.
    Fluid,
};

/// Does a voxel emit a face on this pass?
bool emitsAt(const Scratch& s, Pass pass, i32 x, i32 y, i32 z) {
    switch (pass) {
    case Pass::Opaque: return opaqueAt(s, x, y, z);
    case Pass::Cutout: return cutoutAt(s, x, y, z);
    case Pass::Fluid:  return fluidAt(s, x, y, z);
    }
    return false;
}

/// Does a voxel hide the face of its neighbour on this pass?
///
/// **Opaque hides everything, and each pass additionally hides its own kind.** That
/// second half is what stops the shared boundary between two glass blocks drawing two
/// faces, which reads as a seam through a window and is what vanilla culls too.
///
/// **Cutout does not hide the opaque pass**, which is the asymmetry that makes glass
/// see-through: the stone behind a pane still emits its face, and the pane's own
/// discarded pixels are what let you look at it.
bool hidesAt(const Scratch& s, Pass pass, i32 x, i32 y, i32 z) {
    if (opaqueAt(s, x, y, z)) {
        return true;
    }
    switch (pass) {
    case Pass::Opaque: return false;
    case Pass::Cutout: return cutoutAt(s, x, y, z);
    case Pass::Fluid:  return fluidAt(s, x, y, z);
    }
    return false;
}

u8 lightAt(const Scratch& s, i32 x, i32 y, i32 z) {
    return s.light[paddedIndex(x, y, z)];
}

/// The padded coordinate range one neighbour offset covers.
constexpr std::pair<i32, i32> rangeFor(i32 offset) {
    if (offset < 0) {
        return {-kPad, 0};
    }
    if (offset > 0) {
        return {kN, kN + kPad};
    }
    return {0, kN};
}

/// Reads the neighbourhood once into flat arrays. Every later pass then works on
/// contiguous memory instead of going through palette indirection, and boundary
/// handling stops being a special case anywhere else in the file.
void decodeNeighbourhood(const SectionNeighbourhood& hood, Scratch& s) {
    MC_PROFILE_SCOPE_N("decodeNeighbourhood");

    const BlockRegistry& registry = BlockRegistry::instance();
    const Section* center = hood.center();
    MC_ASSERT_MSG(center != nullptr, "meshing a neighbourhood with no centre section");

    // Zero first, so a null or all-air neighbour costs nothing at all beyond this
    // memset -- which is the common case, since most of a column is sky.
    std::memset(s.opaque.data(), 0, s.opaque.size());
    std::memset(s.fluid.data(), 0, s.fluid.size());
    std::memset(s.cutout.data(), 0, s.cutout.size());
    std::memset(s.light.data(), 0, s.light.size());
    s.anyFluid = false;
    s.anyCutout = false;
    s.anyModel = false;

    // **Sky and block light collapse to one number here and nowhere else.**
    // DESIGN.md 3.7: the quad's sixteen light bits are full, so a torch does not
    // widen the word -- the two channels are combined at mesh time with `max` and the
    // shader is handed exactly what it always was. What that costs is the tint, and
    // only the tint: a torch-lit wall comes out bright but not warm.
    //
    // Uniform only when *both* channels are, since the max of a uniform array and a
    // varying one still varies. `Section::light` does the max per voxel for the rest.
    const bool centerLightUniform =
        center->skyLightArray().isUniform() && center->blockLightArray().isUniform();
    const u8 centerLightLevel = std::max(center->skyLightArray().uniformLevel(),
                                         center->blockLightArray().uniformLevel());

    for (i32 y = 0; y < kN; ++y) {
        for (i32 z = 0; z < kN; ++z) {
            for (i32 x = 0; x < kN; ++x) {
                const usize local = localIndex(x, y, z);
                const BlockId block = center->getByIndex(local);
                s.blocks[local] = block;
                s.opaque[paddedIndex(x, y, z)] = registry.isOpaque(block) ? u8{1} : u8{0};
                if (registry.isFluid(block)) {
                    s.fluid[paddedIndex(x, y, z)] =
                        static_cast<u8>(1u + fluidLevelOf(block));
                    s.anyFluid = true;
                }
                if (isCutout(block)) {
                    s.cutout[paddedIndex(x, y, z)] = 1;
                    s.anyCutout = true;
                }
                // **No occupancy grid for these, on purpose.** A non-cube block is
                // already absent from `opaque` -- a slab is not opaque -- so it emits
                // no cube faces and hides none of its neighbours', which is exactly
                // right. All the model pass needs is to know the section is worth
                // walking, and `s.blocks` already holds which voxels they are.
                if (!isFullCube(block)) {
                    s.anyModel = true;
                }
                s.light[paddedIndex(x, y, z)] =
                    centerLightUniform ? centerLightLevel : center->light(x, y, z);
            }
        }
    }

    // The shell: 34^3 - 32^3 = 6,536 voxels, filled from up to 26 neighbours. The
    // uniform and null cases are handled per neighbour rather than per voxel,
    // because both are far more common than a partially filled neighbour.
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dy = -1; dy <= 1; ++dy) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0 && dz == 0) {
                    continue;
                }

                const Section* section = hood.at(dx, dy, dz);
                if (section == nullptr) {
                    continue; // Already zero: reads as air.
                }

                const auto [xBegin, xEnd] = rangeFor(dx);
                const auto [yBegin, yEnd] = rangeFor(dy);
                const auto [zBegin, zEnd] = rangeFor(dz);

                // Opacity and light are uniform independently of each other: a
                // section can be solid rock throughout and still hold a gradient of
                // light, or be open air throughout and hold one level. So the two
                // fast paths are asked about separately.
                const bool blocksUniform = section->isUniform();
                const bool opaqueFill =
                    blocksUniform && registry.isOpaque(section->uniformBlock());
                const bool fluidFill =
                    blocksUniform && registry.isFluid(section->uniformBlock());
                const bool cutoutFill =
                    blocksUniform && isCutout(section->uniformBlock());
                const bool lightUniform = section->skyLightArray().isUniform()
                                          && section->blockLightArray().isUniform();
                const u8 lightLevel = std::max(section->skyLightArray().uniformLevel(),
                                               section->blockLightArray().uniformLevel());

                if (blocksUniform && !opaqueFill && !fluidFill && !cutoutFill
                    && lightUniform && lightLevel == 0) {
                    continue; // All four already zero.
                }

                for (i32 y = yBegin; y < yEnd; ++y) {
                    for (i32 z = zBegin; z < zEnd; ++z) {
                        for (i32 x = xBegin; x < xEnd; ++x) {
                            const i32 lx = blockToLocalCoord(x);
                            const i32 ly = blockToLocalCoord(y);
                            const i32 lz = blockToLocalCoord(z);

                            const BlockId neighbourBlock =
                                blocksUniform ? section->uniformBlock()
                                              : section->get(lx, ly, lz);

                            s.opaque[paddedIndex(x, y, z)] =
                                blocksUniform
                                    ? (opaqueFill ? u8{1} : u8{0})
                                    : (registry.isOpaque(neighbourBlock) ? u8{1} : u8{0});
                            // The shell's fluid occupancy matters even when the
                            // centre holds none: a water face on the boundary of a
                            // full ocean section must be culled against the water in
                            // the section next door, or every 32-block seam draws a
                            // wall of quads inside the sea.
                            s.fluid[paddedIndex(x, y, z)] =
                                registry.isFluid(neighbourBlock)
                                    ? static_cast<u8>(1u + fluidLevelOf(neighbourBlock))
                                    : u8{0};
                            // The shell's cutout occupancy, for the same reason the
                            // fluid one is here: two glass blocks either side of a
                            // section boundary have to cull against each other, or
                            // every 32-block seam draws a pane inside the window.
                            s.cutout[paddedIndex(x, y, z)] =
                                isCutout(neighbourBlock) ? u8{1} : u8{0};
                            s.light[paddedIndex(x, y, z)] =
                                lightUniform ? lightLevel : section->light(lx, ly, lz);
                        }
                    }
                }
            }
        }
    }
}

/// Fills the emit and cull columns for one pass.
///
/// On the opaque pass the two are identical and this is exactly what it always was.
/// On the fluid pass the emit set is water and the cull set is water plus rock,
/// which is what stops the inside of an ocean being meshed. On the cutout pass the
/// emit set is glass and the cull set is glass plus rock, by the same argument: a
/// wall of panes is a window, not a stack of boxes.
void buildOccupancy(Scratch& s, Pass pass) {
    MC_PROFILE_SCOPE_N("buildOccupancy");

    for (auto& row : s.colX) { row.fill(0); }
    for (auto& row : s.colY) { row.fill(0); }
    for (auto& row : s.colZ) { row.fill(0); }
    for (auto& row : s.cullX) { row.fill(0); }
    for (auto& row : s.cullY) { row.fill(0); }
    for (auto& row : s.cullZ) { row.fill(0); }

    for (i32 y = 0; y < kN; ++y) {
        for (i32 z = 0; z < kN; ++z) {
            for (i32 x = 0; x < kN; ++x) {
                const bool emits = emitsAt(s, pass, x, y, z);
                const bool hides = hidesAt(s, pass, x, y, z);
                if (!emits && !hides) {
                    continue;
                }

                const auto ux = static_cast<u32>(x);
                const auto uy = static_cast<u32>(y);
                const auto uz = static_cast<u32>(z);
                const auto sx = static_cast<usize>(x);
                const auto sy = static_cast<usize>(y);
                const auto sz = static_cast<usize>(z);

                if (emits) {
                    s.colX[sy][sz] |= 1u << ux;
                    s.colY[sx][sz] |= 1u << uy;
                    s.colZ[sx][sy] |= 1u << uz;
                }
                if (hides) {
                    s.cullX[sy][sz] |= 1u << ux;
                    s.cullY[sx][sz] |= 1u << uy;
                    s.cullZ[sx][sy] |= 1u << uz;
                }
            }
        }
    }

    // One value per plane cell, read from the shell decoded above. These are the
    // cull set: they answer "is the face on this boundary hidden by the neighbour",
    // which is the same question the shifted columns answer inside the section.
    for (i32 a = 0; a < kN; ++a) {
        for (i32 b = 0; b < kN; ++b) {
            const auto ua = static_cast<usize>(a);
            const auto ub = static_cast<usize>(b);

            s.lowX[ua][ub] = hidesAt(s, pass, -1, a, b) ? u8{1} : u8{0};
            s.highX[ua][ub] = hidesAt(s, pass, kN, a, b) ? u8{1} : u8{0};

            s.lowY[ua][ub] = hidesAt(s, pass, a, -1, b) ? u8{1} : u8{0};
            s.highY[ua][ub] = hidesAt(s, pass, a, kN, b) ? u8{1} : u8{0};

            s.lowZ[ua][ub] = hidesAt(s, pass, a, b, -1) ? u8{1} : u8{0};
            s.highZ[ua][ub] = hidesAt(s, pass, a, b, kN) ? u8{1} : u8{0};
        }
    }
}

/// The core of binary greedy meshing: 32 faces resolved per instruction.
///
/// A face exists at bit i when the voxel there is solid and its neighbour along
/// the axis is not. Shifting the column against itself answers that for all 32
/// voxels at once. The shift brings in a zero at the far end, which used to be
/// the "outside is air" boundary rule; now the neighbour's real occupancy is
/// shifted in there instead, so a chunk seam culls exactly like any interior face
/// and no redundant wall of quads is emitted.
void buildFaceMasks(Scratch& s) {
    MC_PROFILE_SCOPE_N("buildFaceMasks");

    for (usize a = 0; a < kN; ++a) {
        for (usize b = 0; b < kN; ++b) {
            // The emit column decides which voxels *have* a face; the cull column
            // decides which of those faces are hidden. They are the same array on
            // the opaque pass, so this is unchanged there.
            const u32 cx = s.colX[a][b];
            const u32 hx = s.cullX[a][b];
            s.faceMask[static_cast<usize>(Face::NegX)][a][b] =
                cx & ~((hx << 1) | static_cast<u32>(s.lowX[a][b]));
            s.faceMask[static_cast<usize>(Face::PosX)][a][b] =
                cx & ~((hx >> 1) | (static_cast<u32>(s.highX[a][b]) << 31));

            const u32 cy = s.colY[a][b];
            const u32 hy = s.cullY[a][b];
            s.faceMask[static_cast<usize>(Face::NegY)][a][b] =
                cy & ~((hy << 1) | static_cast<u32>(s.lowY[a][b]));
            s.faceMask[static_cast<usize>(Face::PosY)][a][b] =
                cy & ~((hy >> 1) | (static_cast<u32>(s.highY[a][b]) << 31));

            const u32 cz = s.colZ[a][b];
            const u32 hz = s.cullZ[a][b];
            s.faceMask[static_cast<usize>(Face::NegZ)][a][b] =
                cz & ~((hz << 1) | static_cast<u32>(s.lowZ[a][b]));
            s.faceMask[static_cast<usize>(Face::PosZ)][a][b] =
                cz & ~((hz >> 1) | (static_cast<u32>(s.highZ[a][b]) << 31));
        }
    }
}

/// Recovers the voxel a face belongs to from its mask position.
///
/// The mask for an axis is indexed [a][b] with the bit selecting the position
/// along that axis, so the mapping is just a permutation.
void voxelFromMask(const FacePlan& plan, i32 a, i32 b, i32 p, i32& x, i32& y, i32& z) {
    if (plan.nx != 0) {
        x = p; y = a; z = b;
    } else if (plan.ny != 0) {
        x = a; y = p; z = b;
    } else {
        x = a; y = b; z = p;
    }
}

/// Standard voxel AO: each corner is darkened by the two edge neighbours and
/// the diagonal one in the layer above the face. Two adjacent edges fully
/// occlude regardless of the diagonal.
///
/// Every sample goes through the padded grid, so a face on a section boundary is
/// occluded by the neighbouring section's blocks just as an interior one is. This
/// is the half of neighbour awareness that is easy to forget and impossible to
/// miss once seen: without it, every chunk seam carries a bright rim.
u8 computeAo(const Scratch& s, const FacePlan& plan, i32 x, i32 y, i32 z) {
    const i32 bx = x + plan.nx;
    const i32 by = y + plan.ny;
    const i32 bz = z + plan.nz;

    u8 packed = 0;
    for (u32 corner = 0; corner < 4; ++corner) {
        // Corner order matches kCorners in chunk.vert: (0,0) (1,0) (1,1) (0,1).
        const i32 su = (corner == 1 || corner == 2) ? 1 : -1;
        const i32 sv = (corner == 2 || corner == 3) ? 1 : -1;

        const bool side1 = opaqueAt(s, bx + plan.ux * su, by + plan.uy * su, bz + plan.uz * su);
        const bool side2 = opaqueAt(s, bx + plan.vx * sv, by + plan.vy * sv, bz + plan.vz * sv);
        const bool diagonal = opaqueAt(s,
                                       bx + plan.ux * su + plan.vx * sv,
                                       by + plan.uy * su + plan.vy * sv,
                                       bz + plan.uz * su + plan.vz * sv);

        const u32 value = (side1 && side2)
                              ? 0u
                              : 3u - (static_cast<u32>(side1) + static_cast<u32>(side2)
                                      + static_cast<u32>(diagonal));
        packed |= static_cast<u8>(value << (corner * 2));
    }
    return packed;
}

/// Smooth lighting: each corner takes the mean of the light in the four cells that
/// touch it on the lit side of the face.
///
/// The same four cells AO looks at, and that is not a coincidence -- AO asks how
/// many of them are solid, this asks how bright the ones that are not are. Solid
/// cells are skipped rather than counted as dark, or every corner against a wall
/// would read as a shadow that the AO term is already drawing.
u16 computeCornerLight(const Scratch& s, const FacePlan& plan, i32 x, i32 y, i32 z) {
    const i32 bx = x + plan.nx;
    const i32 by = y + plan.ny;
    const i32 bz = z + plan.nz;

    u16 packed = 0;
    for (u32 corner = 0; corner < 4; ++corner) {
        // Corner order matches computeAo and kCorners in chunk.vert.
        const i32 su = (corner == 1 || corner == 2) ? 1 : -1;
        const i32 sv = (corner == 2 || corner == 3) ? 1 : -1;

        // The cell the face looks into is transparent by construction -- that is
        // why the face exists -- so there is always at least one sample.
        u32 total = lightAt(s, bx, by, bz);
        u32 count = 1;

        const auto take = [&](i32 ox, i32 oy, i32 oz) {
            if (opaqueAt(s, ox, oy, oz)) {
                return;
            }
            total += lightAt(s, ox, oy, oz);
            ++count;
        };

        take(bx + plan.ux * su, by + plan.uy * su, bz + plan.uz * su);
        take(bx + plan.vx * sv, by + plan.vy * sv, bz + plan.vz * sv);
        take(bx + plan.ux * su + plan.vx * sv,
             by + plan.uy * su + plan.vy * sv,
             bz + plan.uz * su + plan.vz * sv);

        const u32 level = (total + count / 2) / count; // rounded mean
        packed |= static_cast<u16>(level << (corner * 4));
    }
    return packed;
}

/// Nearest of the four drops the water shader can draw, given one in ninths.
///
/// The shader's table is {0, 1, 4, 8} ninths: a full block, vanilla's source
/// surface, and two steps down to the thin edge of a run. Bands rather than a
/// divide because the ends are not evenly spaced -- almost all water in the world
/// is either submerged or a source, so those two get an exact code each and the
/// five flowing levels share the remaining two.
u8 quantizeFluidDrop(u32 ninths) {
    if (ninths == 0) {
        return 0;
    }
    if (ninths <= 2) {
        return 1;
    }
    if (ninths <= 6) {
        return 2;
    }
    return 3;
}

/// Four corner drops for one fluid face, two bits each, in the corner order AO and
/// light already use.
///
/// **Every vertex is asked the same question -- "how far down does the surface sit
/// here" -- and a vertex that is not on the top of its block answers zero.** That is
/// what lets one field serve all six faces with no per-face logic in the shader: a
/// top face gets four real drops, a bottom face gets four zeroes, and a side face
/// gets two and two, which lowers its upper edge to meet the surface exactly. Doing
/// it any other way means the side of a stream stands a fraction of a block proud of
/// its own top and the gap is lit from inside.
///
/// The drop of a lattice corner is the mean over the fluid blocks meeting there, the
/// way vanilla averages its corner heights. A block with fluid directly above it is
/// submerged and contributes a full block: the surface is not there, it is further
/// up. Non-fluid neighbours are left out of the mean rather than counted as empty,
/// so water against a wall keeps its height instead of being dragged to the floor.
u8 computeFluidCorners(const Scratch& s, const FacePlan& plan, i32 x, i32 y, i32 z) {
    // Where this face's own plane sits within the voxel, in {0,1}^3.
    const i32 faceX = plan.nx > 0 ? 1 : 0;
    const i32 faceY = plan.ny > 0 ? 1 : 0;
    const i32 faceZ = plan.nz > 0 ? 1 : 0;

    u8 packed = 0;
    for (u32 corner = 0; corner < 4; ++corner) {
        // Corner order matches computeAo and kCorners in chunk.vert.
        const i32 cu = (corner == 1 || corner == 2) ? 1 : 0;
        const i32 cv = (corner == 2 || corner == 3) ? 1 : 0;

        const i32 ox = faceX + plan.ux * cu + plan.vx * cv;
        const i32 oy = faceY + plan.uy * cu + plan.vy * cv;
        const i32 oz = faceZ + plan.uz * cu + plan.vz * cv;

        if (oy != 1) {
            continue; // Not on the top of the block: no drop, and 0 is already there.
        }

        // The up-to-four blocks sharing the horizontal lattice corner this vertex
        // stands on. The owning voxel is always one of them, whichever way ox and oz
        // came out, so the mean never divides by zero on a face that exists.
        u32 sum = 0;
        u32 count = 0;
        for (i32 dz = -1; dz <= 0; ++dz) {
            for (i32 dx = -1; dx <= 0; ++dx) {
                const i32 bx = x + ox + dx;
                const i32 bz = z + oz + dz;
                if (!fluidAt(s, bx, y, bz)) {
                    continue;
                }
                ++count;
                if (fluidAt(s, bx, y + 1, bz)) {
                    continue; // Submerged: full block, drop 0.
                }
                // The stored byte is 1 + level, which is the drop in ninths: a
                // source sits one ninth down and level 7 sits eight.
                sum += s.fluid[paddedIndex(bx, y, bz)];
            }
        }
        if (count == 0) {
            continue;
        }

        const u32 mean = (sum + count / 2) / count;
        packed |= static_cast<u8>(quantizeFluidDrop(mean) << (2u * corner));
    }
    return packed;
}

/// Maps (plane, u, v) back to the voxel that owns the face.
void voxelFor(const FacePlan& plan, i32 p, i32 u, i32 v, i32& x, i32& y, i32& z) {
    x = plan.nx != 0 ? p : (plan.ux != 0 ? u : v);
    y = plan.ny != 0 ? p : (plan.uy != 0 ? u : v);
    z = plan.nz != 0 ? p : (plan.uz != 0 ? u : v);
}

/// Appends one word per box of every non-cube block in the centre section.
///
/// **Nothing merges here, and that is the difference from every other pass.** The
/// greedy method exists because a wall of stone is thousands of coplanar faces that
/// are one quad; a slab's geometry stops at its own cell, and two slabs side by side
/// share no face a merge could remove. So this is a walk, and it is affordable for the
/// reason the whole format is: a house holds a few hundred of these against the
/// millions of quads the terrain around it is worth.
///
/// **Light comes from the neighbours, not from the block's own cell**, and getting
/// that wrong once is what this paragraph is for. A slab shades what is under it --
/// `shadesLight` is true, which is vanilla's rule -- so `computeSkyLight` writes zero
/// into the cell it stands in, and a box lit from there comes out black. The cube path
/// never had this problem because a face is lit by the cell it *faces*, which is why
/// `Scratch::light` is a padded grid in the first place.
///
/// The brightest of the six neighbours, rather than the cell above alone. Above is the
/// right answer for a slab in the open and the wrong one for a slab under a ceiling,
/// where the light arrives from the side; six reads settle both and cost nothing at
/// this frequency.
///
/// **One value for the whole box, and it is `max(sky, block)` already combined.** The
/// word has room to keep the two apart -- it is laid out that way, because a day/night
/// cycle needs the separation -- but the mesher has only the combined padded grid to
/// read, so the sky field carries the answer and the block field is zero. Splitting
/// them is a second padded grid in `Scratch`, and it belongs with the cycle that needs
/// it rather than here.
u8 neighbourLight(const Scratch& s, i32 x, i32 y, i32 z) {
    u8 best = s.light[paddedIndex(x, y + 1, z)];
    best = std::max(best, s.light[paddedIndex(x, y - 1, z)]);
    best = std::max(best, s.light[paddedIndex(x - 1, y, z)]);
    best = std::max(best, s.light[paddedIndex(x + 1, y, z)]);
    best = std::max(best, s.light[paddedIndex(x, y, z - 1)]);
    best = std::max(best, s.light[paddedIndex(x, y, z + 1)]);
    return best;
}

void emitModelBoxes(const Scratch& s, ChunkMesh& out, const BlockRegistry& registry) {
    for (i32 y = 0; y < kN; ++y) {
        for (i32 z = 0; z < kN; ++z) {
            for (i32 x = 0; x < kN; ++x) {
                const BlockId block = s.blocks[localIndex(x, y, z)];
                if (isFullCube(block)) {
                    continue;
                }
                const std::span<const BlockBox> boxes = blockBoxes(block);
                if (boxes.empty()) {
                    continue; // Air and water reach here and have no geometry.
                }

                // One layer for the whole box rather than one per face. A slab is cut
                // from a block, so every face of it is that block's side texture --
                // and `PosY` is what a top face asks for, which is the one a player
                // looks at.
                const u16 layer = registry.textureLayer(block, Face::PosY);
                const u8 light = neighbourLight(s, x, y, z);

                for (const BlockBox& box : boxes) {
                    out.quads.push_back(
                        ModelBox::make(static_cast<u32>(x), static_cast<u32>(y),
                                       static_cast<u32>(z), box, layer, light,
                                       /*blockLight=*/0)
                            .asQuad());
                }
            }
        }
    }
}

} // namespace

void meshSectionGreedy(const SectionNeighbourhood& hood,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options) {
    MC_PROFILE_SCOPE_N("meshSectionGreedy");

    out.clear();

    const Section* center = hood.center();
    MC_ASSERT_MSG(center != nullptr, "meshing a neighbourhood with no centre section");

    // An all-air centre produces nothing whatever the neighbours hold: a face
    // belongs to the solid voxel behind it, and there are none here. The check
    // costs one comparison, because uniform sections carry no index array.
    if (center->isEmpty()) {
        return;
    }

    Scratch& s = scratch();
    decodeNeighbourhood(hood, s);

    const BlockRegistry& registry = BlockRegistry::instance();

    // Opaque, then cutout, then fluid, each appended after the last. The two split
    // points are what let the renderer draw three passes over one contiguous arena
    // range -- the order here *is* the layout, and `ChunkMesh` records where the
    // joins fell.
    for (const Pass pass : {Pass::Opaque, Pass::Cutout, Pass::Fluid}) {
        // What two neighbouring faces must agree on to merge into one quad.
        //
        // A mask rather than a shift, because the optional field is no longer the
        // lowest one: light has to stay in the key whatever AO does. Merging across
        // a light boundary would take one corner's brightness and stretch it over
        // both faces, which is a far more visible error than a lost merge -- it puts
        // a hard edge of the wrong shade across the middle of a cave wall.
        //
        // **`aoAwareMerging` may not reach the fluid pass, and this is the trap that
        // field's reuse sets.** Those low eight bits are corner drops on a fluid
        // quad, not AO, and dropping them from the key merges a sloping surface into
        // a flat one -- turning the option that trades merge ratio against shading
        // into one that silently flattens water. The fluid pass always keys on the
        // whole word.
        const u32 keyMask =
            (pass != Pass::Opaque || options.aoAwareMerging) ? 0xFFFFFFFFu : 0xFFFFFF00u;

        if (pass == Pass::Cutout) {
            out.opaqueQuads = out.quads.size();

            // **Between opaque and cutout, and before either early exit below.** The
            // model boxes are depth-writing geometry like the opaque pass, so they
            // belong on that side of the alpha test; and putting the call here rather
            // than inside the `anyCutout` branch is what keeps a section that holds
            // slabs and nothing else from losing them to a `break`.
            if (s.anyModel) {
                emitModelBoxes(s, out, registry);
            }
            out.modelBoxes = out.quads.size() - out.opaqueQuads;

            if (!s.anyCutout) {
                // Every section nobody has glazed, which is all of them until
                // somebody does. Costs one boolean.
                out.cutoutQuads = 0;
                if (!s.anyFluid) {
                    break; // Nothing left for the fluid pass either.
                }
                continue;
            }
        }

        if (pass == Pass::Fluid) {
            out.cutoutQuads = out.quads.size() - out.opaqueQuads - out.modelBoxes;
            if (!s.anyFluid) {
                // Almost every section in the world: everything above sea level and
                // everything below the sea bed. The whole third pass costs one
                // boolean for them.
                break;
            }
        }

        buildOccupancy(s, pass);
        buildFaceMasks(s);

    for (usize faceIndex = 0; faceIndex < kFaceCount; ++faceIndex) {
        const FacePlan& plan = kPlans[faceIndex];
        const Mask2D& mask = s.faceMask[faceIndex];

        for (auto& row : s.planeRows) {
            row.fill(0);
        }
        u32 occupiedPlanes = 0;

        // Scatter: walk only the bits that are actually set. The whole point of
        // building the masks was to avoid touching the ~192,000 cells that hold
        // no face; iterating the planes densely here would have thrown that
        // away, which is exactly the mistake the first version of this made.
        for (i32 a = 0; a < kN; ++a) {
            for (i32 b = 0; b < kN; ++b) {
                u32 bits = mask[static_cast<usize>(a)][static_cast<usize>(b)];
                while (bits != 0) {
                    const auto p = static_cast<i32>(std::countr_zero(bits));
                    bits &= bits - 1;

                    i32 x = 0;
                    i32 y = 0;
                    i32 z = 0;
                    voxelFromMask(plan, a, b, p, x, y, z);

                    const i32 u = plan.uFromA ? a : b;
                    const i32 v = plan.uFromA ? b : a;

                    const BlockId block = s.blocks[localIndex(x, y, z)];
                    const u16 layer = registry.textureLayer(block, plan.face);
                    // The same eight bits, and two different meanings. See Quad.hpp:
                    // AO on water is meaningless, which is what freed the field for
                    // the surface height that had nowhere else to go.
                    const u8 ao =
                        pass == Pass::Fluid ? computeFluidCorners(s, plan, x, y, z)
                        : options.ambientOcclusion ? computeAo(s, plan, x, y, z)
                                                   : 0;
                    const u16 light = computeCornerLight(s, plan, x, y, z);

                    s.planeRows[static_cast<usize>(p)][static_cast<usize>(v)] |=
                        1u << static_cast<u32>(u);
                    s.cells[static_cast<usize>(p)][static_cast<usize>(v)][static_cast<usize>(u)] =
                        (static_cast<u32>(layer) << 24) | (static_cast<u32>(light) << 8) | ao;
                    occupiedPlanes |= 1u << static_cast<u32>(p);
                }
            }
        }

        // Merge, skipping planes with no faces at all.
        while (occupiedPlanes != 0) {
            const auto p = static_cast<i32>(std::countr_zero(occupiedPlanes));
            occupiedPlanes &= occupiedPlanes - 1;

            auto& rows = s.planeRows[static_cast<usize>(p)];
            const auto& cells = s.cells[static_cast<usize>(p)];

            for (i32 v = 0; v < kN; ++v) {
                const auto uv = static_cast<usize>(v);
                while (rows[uv] != 0) {
                    const auto u = static_cast<i32>(std::countr_zero(rows[uv]));
                    const auto uu = static_cast<usize>(u);
                    const u32 cell = cells[uv][uu];
                    const u32 key = cell & keyMask;

                    // Extend along U while the key matches.
                    i32 width = 1;
                    while (u + width < kN
                           && ((rows[uv] >> static_cast<u32>(u + width)) & 1u) != 0
                           && (cells[uv][static_cast<usize>(u + width)] & keyMask) == key) {
                        ++width;
                    }

                    const u32 runMask = (width == kN)
                                            ? 0xFFFFFFFFu
                                            : (((1u << static_cast<u32>(width)) - 1u)
                                               << static_cast<u32>(u));

                    // Extend along V while the whole run matches.
                    i32 height = 1;
                    while (v + height < kN) {
                        const auto next = static_cast<usize>(v + height);
                        if ((rows[next] & runMask) != runMask) {
                            break;
                        }
                        bool sameKey = true;
                        for (i32 d = 0; d < width; ++d) {
                            if ((cells[next][static_cast<usize>(u + d)] & keyMask) != key) {
                                sameKey = false;
                                break;
                            }
                        }
                        if (!sameKey) {
                            break;
                        }
                        ++height;
                    }

                    for (i32 d = 0; d < height; ++d) {
                        rows[static_cast<usize>(v + d)] &= ~runMask;
                    }

                    i32 x = 0;
                    i32 y = 0;
                    i32 z = 0;
                    voxelFor(plan, p, u, v, x, y, z);

                    out.quads.push_back(Quad::make(static_cast<u32>(x + (plan.nx > 0 ? 1 : 0)),
                                                   static_cast<u32>(y + (plan.ny > 0 ? 1 : 0)),
                                                   static_cast<u32>(z + (plan.nz > 0 ? 1 : 0)),
                                                   static_cast<u32>(width),
                                                   static_cast<u32>(height),
                                                   plan.face,
                                                   static_cast<u16>(cell >> 24),
                                                   static_cast<u8>(cell & 0xFFu),
                                                   static_cast<u16>((cell >> 8) & 0xFFFFu)));
                }
            }
        }
    }
    } // pass
}

void meshSectionGreedy(const Section& section,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options) {
    SectionNeighbourhood hood;
    hood.set(0, 0, 0, &section);
    meshSectionGreedy(hood, out, options);
}

} // namespace mc
