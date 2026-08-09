#include "mesh/BinaryGreedyMesher.hpp"

#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <array>
#include <bit>

namespace mc {
namespace {

constexpr i32 kN = kSectionSize; // 32
static_assert(kN == 32, "the occupancy masks below are 32 bits wide");

/// [a][b] -> bitmask over the third axis.
using Mask2D = std::array<std::array<u32, kN>, kN>;

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
/// ~100 KiB of allocation per section without any ownership plumbing.
struct Scratch {
    std::array<BlockId, kSectionVolume> blocks{};
    std::array<bool, kSectionVolume> opaque{};

    Mask2D colX{}; ///< [y][z], bit x
    Mask2D colY{}; ///< [x][z], bit y
    Mask2D colZ{}; ///< [x][y], bit z

    std::array<Mask2D, kFaceCount> faceMask{};

    /// Per-plane merge state for the face currently being processed.
    /// planeRows[plane][v] is a bitmask over u; cells[plane][v][u] holds
    /// (layer << 8) | ao and is only meaningful where the row bit is set, so it
    /// never has to be cleared.
    std::array<std::array<u32, kN>, kN> planeRows{};
    std::array<std::array<std::array<u32, kN>, kN>, kN> cells{};
};

Scratch& scratch() {
    static thread_local Scratch instance;
    return instance;
}

constexpr bool inRange(i32 v) {
    return v >= 0 && v < kN;
}

bool opaqueAt(const Scratch& s, i32 x, i32 y, i32 z) {
    if (!inRange(x) || !inRange(y) || !inRange(z)) {
        return false; // Outside an isolated section counts as air.
    }
    return s.opaque[localIndex(x, y, z)];
}

/// Reads the section once into flat arrays. Every later pass then works on
/// contiguous memory instead of going through palette indirection.
void decodeSection(const Section& section, Scratch& s) {
    MC_PROFILE_SCOPE_N("decodeSection");

    const BlockRegistry& registry = BlockRegistry::instance();
    for (usize i = 0; i < kSectionVolume; ++i) {
        const BlockId block = section.getByIndex(i);
        s.blocks[i] = block;
        s.opaque[i] = registry.isOpaque(block);
    }
}

void buildOccupancy(Scratch& s) {
    MC_PROFILE_SCOPE_N("buildOccupancy");

    for (auto& row : s.colX) { row.fill(0); }
    for (auto& row : s.colY) { row.fill(0); }
    for (auto& row : s.colZ) { row.fill(0); }

    for (i32 y = 0; y < kN; ++y) {
        for (i32 z = 0; z < kN; ++z) {
            for (i32 x = 0; x < kN; ++x) {
                if (!s.opaque[localIndex(x, y, z)]) {
                    continue;
                }
                const auto ux = static_cast<u32>(x);
                const auto uy = static_cast<u32>(y);
                const auto uz = static_cast<u32>(z);
                s.colX[static_cast<usize>(y)][static_cast<usize>(z)] |= 1u << ux;
                s.colY[static_cast<usize>(x)][static_cast<usize>(z)] |= 1u << uy;
                s.colZ[static_cast<usize>(x)][static_cast<usize>(y)] |= 1u << uz;
            }
        }
    }
}

/// The core of binary greedy meshing: 32 faces resolved per instruction.
///
/// A face exists at bit i when the voxel there is solid and its neighbour along
/// the axis is not. Shifting the column against itself answers that for all 32
/// voxels at once. The shift brings in a zero at the far end, which is exactly
/// the "outside is air" boundary rule; suppressing boundary faces is then a
/// single extra mask.
void buildFaceMasks(Scratch& s, bool emitBoundaryFaces) {
    MC_PROFILE_SCOPE_N("buildFaceMasks");

    constexpr u32 kNoLastBit = 0x7FFFFFFFu;
    constexpr u32 kNoFirstBit = 0xFFFFFFFEu;

    const auto negTrim = emitBoundaryFaces ? 0xFFFFFFFFu : kNoFirstBit;
    const auto posTrim = emitBoundaryFaces ? 0xFFFFFFFFu : kNoLastBit;

    for (usize a = 0; a < kN; ++a) {
        for (usize b = 0; b < kN; ++b) {
            const u32 cx = s.colX[a][b];
            s.faceMask[static_cast<usize>(Face::NegX)][a][b] = cx & ~(cx << 1) & negTrim;
            s.faceMask[static_cast<usize>(Face::PosX)][a][b] = cx & ~(cx >> 1) & posTrim;

            const u32 cy = s.colY[a][b];
            s.faceMask[static_cast<usize>(Face::NegY)][a][b] = cy & ~(cy << 1) & negTrim;
            s.faceMask[static_cast<usize>(Face::PosY)][a][b] = cy & ~(cy >> 1) & posTrim;

            const u32 cz = s.colZ[a][b];
            s.faceMask[static_cast<usize>(Face::NegZ)][a][b] = cz & ~(cz << 1) & negTrim;
            s.faceMask[static_cast<usize>(Face::PosZ)][a][b] = cz & ~(cz >> 1) & posTrim;
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

/// Maps (plane, u, v) back to the voxel that owns the face.
void voxelFor(const FacePlan& plan, i32 p, i32 u, i32 v, i32& x, i32& y, i32& z) {
    x = plan.nx != 0 ? p : (plan.ux != 0 ? u : v);
    y = plan.ny != 0 ? p : (plan.uy != 0 ? u : v);
    z = plan.nz != 0 ? p : (plan.uz != 0 ? u : v);
}

} // namespace

void meshSectionGreedy(const Section& section,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options) {
    MC_PROFILE_SCOPE_N("meshSectionGreedy");

    out.clear();
    if (section.isEmpty()) {
        return;
    }

    Scratch& s = scratch();
    decodeSection(section, s);
    buildOccupancy(s);
    buildFaceMasks(s, options.emitBoundaryFaces);

    const BlockRegistry& registry = BlockRegistry::instance();
    const u32 keyShift = options.aoAwareMerging ? 0u : 8u;

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

                    s.planeRows[static_cast<usize>(p)][static_cast<usize>(v)] |=
                        1u << static_cast<u32>(u);
                    s.cells[static_cast<usize>(p)][static_cast<usize>(v)][static_cast<usize>(u)] =
                        (static_cast<u32>(layer) << 8) | ao;
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
                    const u32 key = cell >> keyShift;

                    // Extend along U while the key matches.
                    i32 width = 1;
                    while (u + width < kN
                           && ((rows[uv] >> static_cast<u32>(u + width)) & 1u) != 0
                           && (cells[uv][static_cast<usize>(u + width)] >> keyShift) == key) {
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
                            if ((cells[next][static_cast<usize>(u + d)] >> keyShift) != key) {
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
                                                   static_cast<u16>(cell >> 8),
                                                   static_cast<u8>(cell & 0xFFu)));
                }
            }
        }
    }
}

} // namespace mc
