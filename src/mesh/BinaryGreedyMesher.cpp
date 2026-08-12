#include "mesh/BinaryGreedyMesher.hpp"

#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <array>
#include <bit>
#include <cstring>

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
    std::array<u8, kPaddedVolume> fluid{};
    /// Whether the centre section holds any fluid at all. When it does not, the
    /// second pass is skipped whole -- which is almost every section in the world,
    /// so the cost of water existing is one boolean for everything above sea level
    /// and everything under the sea bed.
    bool anyFluid = false;

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

/// Does a voxel emit a face on this pass?
bool emitsAt(const Scratch& s, bool fluidPass, i32 x, i32 y, i32 z) {
    return fluidPass ? fluidAt(s, x, y, z) : opaqueAt(s, x, y, z);
}

/// Does a voxel hide the face of its neighbour on this pass?
bool hidesAt(const Scratch& s, bool fluidPass, i32 x, i32 y, i32 z) {
    return opaqueAt(s, x, y, z) || (fluidPass && fluidAt(s, x, y, z));
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
    std::memset(s.light.data(), 0, s.light.size());
    s.anyFluid = false;

    const bool centerLightUniform = center->skyLightArray().isUniform();
    const u8 centerLightLevel = center->skyLightArray().uniformLevel();

    for (i32 y = 0; y < kN; ++y) {
        for (i32 z = 0; z < kN; ++z) {
            for (i32 x = 0; x < kN; ++x) {
                const usize local = localIndex(x, y, z);
                const BlockId block = center->getByIndex(local);
                s.blocks[local] = block;
                s.opaque[paddedIndex(x, y, z)] = registry.isOpaque(block) ? u8{1} : u8{0};
                if (registry.isFluid(block)) {
                    s.fluid[paddedIndex(x, y, z)] = 1;
                    s.anyFluid = true;
                }
                s.light[paddedIndex(x, y, z)] =
                    centerLightUniform ? centerLightLevel : center->skyLight(x, y, z);
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
                const bool lightUniform = section->skyLightArray().isUniform();
                const u8 lightLevel = section->skyLightArray().uniformLevel();

                if (blocksUniform && !opaqueFill && !fluidFill && lightUniform
                    && lightLevel == 0) {
                    continue; // All three already zero.
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
                                blocksUniform
                                    ? (fluidFill ? u8{1} : u8{0})
                                    : (registry.isFluid(neighbourBlock) ? u8{1} : u8{0});
                            s.light[paddedIndex(x, y, z)] =
                                lightUniform ? lightLevel : section->skyLight(lx, ly, lz);
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
/// which is what stops the inside of an ocean being meshed.
void buildOccupancy(Scratch& s, bool fluidPass) {
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
                const bool emits = emitsAt(s, fluidPass, x, y, z);
                const bool hides = hidesAt(s, fluidPass, x, y, z);
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

            s.lowX[ua][ub] = hidesAt(s, fluidPass, -1, a, b) ? u8{1} : u8{0};
            s.highX[ua][ub] = hidesAt(s, fluidPass, kN, a, b) ? u8{1} : u8{0};

            s.lowY[ua][ub] = hidesAt(s, fluidPass, a, -1, b) ? u8{1} : u8{0};
            s.highY[ua][ub] = hidesAt(s, fluidPass, a, kN, b) ? u8{1} : u8{0};

            s.lowZ[ua][ub] = hidesAt(s, fluidPass, a, b, -1) ? u8{1} : u8{0};
            s.highZ[ua][ub] = hidesAt(s, fluidPass, a, b, kN) ? u8{1} : u8{0};
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

/// Maps (plane, u, v) back to the voxel that owns the face.
void voxelFor(const FacePlan& plan, i32 p, i32 u, i32 v, i32& x, i32& y, i32& z) {
    x = plan.nx != 0 ? p : (plan.ux != 0 ? u : v);
    y = plan.ny != 0 ? p : (plan.uy != 0 ? u : v);
    z = plan.nz != 0 ? p : (plan.uz != 0 ? u : v);
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

    // What two neighbouring faces must agree on to merge into one quad.
    //
    // A mask rather than a shift, because the optional field is no longer the
    // lowest one: light has to stay in the key whatever AO does. Merging across a
    // light boundary would take one corner's brightness and stretch it over both
    // faces, which is a far more visible error than a lost merge -- it puts a hard
    // edge of the wrong shade across the middle of a cave wall.
    const u32 keyMask = options.aoAwareMerging ? 0xFFFFFFFFu : 0xFFFFFF00u;

    // Opaque first, then fluid, appended after it. The split is what lets the
    // renderer draw the two as separate passes over one contiguous arena range.
    for (const bool fluidPass : {false, true}) {
        if (fluidPass) {
            out.opaqueQuads = out.quads.size();
            if (!s.anyFluid) {
                // Almost every section in the world: everything above sea level and
                // everything below the sea bed. The whole second pass costs one
                // boolean for them.
                break;
            }
        }

        buildOccupancy(s, fluidPass);
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
                    const u8 ao = options.ambientOcclusion ? computeAo(s, plan, x, y, z) : 0;
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
    } // fluidPass
}

void meshSectionGreedy(const Section& section,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options) {
    SectionNeighbourhood hood;
    hood.set(0, 0, 0, &section);
    meshSectionGreedy(hood, out, options);
}

} // namespace mc
