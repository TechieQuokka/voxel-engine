#include "mesh/CulledMesher.hpp"

#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <array>

namespace mc {
namespace {

struct FaceDef {
    Face face;
    i32 dx;
    i32 dy;
    i32 dz;
    /// Offset added to the voxel corner to reach the quad's origin. Positive
    /// faces sit on the far plane of the cell, negative faces on the near one.
    i32 ox;
    i32 oy;
    i32 oz;
};

// Order matches the Face enum. The tangent basis for each face lives in the
// shader (chunk.vert); both sides must agree.
constexpr std::array<FaceDef, kFaceCount> kFaces{{
    {Face::NegX, -1, 0, 0, 0, 0, 0},
    {Face::PosX, +1, 0, 0, 1, 0, 0},
    {Face::NegY, 0, -1, 0, 0, 0, 0},
    {Face::PosY, 0, +1, 0, 0, 1, 0},
    {Face::NegZ, 0, 0, -1, 0, 0, 0},
    {Face::PosZ, 0, 0, +1, 0, 0, 1},
}};

constexpr bool inSection(i32 x, i32 y, i32 z) {
    return x >= 0 && x < kSectionSize
        && y >= 0 && y < kSectionSize
        && z >= 0 && z < kSectionSize;
}

} // namespace

void meshSectionCulled(const Section& section, ChunkMesh& out, const MeshOptions& options) {
    MC_PROFILE_SCOPE_N("meshSectionCulled");

    out.clear();

    // An all-air section produces nothing, and this check costs one comparison
    // because uniform sections carry no index array at all.
    if (section.isEmpty()) {
        return;
    }

    const BlockRegistry& blocks = BlockRegistry::instance();

    for (i32 y = 0; y < kSectionSize; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                const BlockId block = section.get(x, y, z);
                if (!blocks.isOpaque(block)) {
                    continue;
                }

                for (const FaceDef& def : kFaces) {
                    const i32 nx = x + def.dx;
                    const i32 ny = y + def.dy;
                    const i32 nz = z + def.dz;

                    const bool hidden =
                        inSection(nx, ny, nz)
                            ? blocks.isOpaque(section.get(nx, ny, nz))
                            : !options.emitBoundaryFaces;
                    if (hidden) {
                        continue;
                    }

                    // A texture array layer, not the BlockId. Both meshers must
                    // agree on what the material field means, or a mesh from
                    // this one would be textured wrongly -- and it stays
                    // renderable precisely so it can be compared against the
                    // greedy mesher on screen, not just in tests.
                    out.quads.push_back(Quad::make(static_cast<u32>(x + def.ox),
                                                   static_cast<u32>(y + def.oy),
                                                   static_cast<u32>(z + def.oz),
                                                   1, 1,
                                                   def.face,
                                                   blocks.textureLayer(block, def.face)));
                }
            }
        }
    }
}

} // namespace mc
