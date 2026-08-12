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

} // namespace

void meshSectionCulled(const SectionNeighbourhood& hood,
                       ChunkMesh& out,
                       const MeshOptions& /*options*/) {
    MC_PROFILE_SCOPE_N("meshSectionCulled");

    out.clear();

    const Section* center = hood.center();
    MC_ASSERT_MSG(center != nullptr, "meshing a neighbourhood with no centre section");

    // An all-air section produces nothing, and this check costs one comparison
    // because uniform sections carry no index array at all.
    if (center->isEmpty()) {
        return;
    }

    const BlockRegistry& blocks = BlockRegistry::instance();

    // Two passes, opaque then fluid, because the mesh is one list with a split and
    // the translucent half has to come second. See ChunkMesh::opaqueQuads.
    //
    // The only difference between them is which blocks emit a face and what counts
    // as hiding one. Opaque: emit for opaque blocks, hidden by an opaque neighbour.
    // Fluid: emit for fluid blocks, hidden by a fluid *or* opaque neighbour -- so
    // water against water produces nothing and an ocean is a surface rather than a
    // stack of visible cubes.
    for (const bool fluidPass : {false, true}) {
        if (fluidPass) {
            // Everything emitted so far was opaque. Recorded here rather than after
            // the loop so that an early return leaves it at the zero `clear()` set.
            out.opaqueQuads = out.quads.size();
        }

        for (i32 y = 0; y < kSectionSize; ++y) {
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    const BlockId block = center->get(x, y, z);
                    const bool emits = fluidPass ? blocks.isFluid(block)
                                                 : blocks.isOpaque(block);
                    if (!emits) {
                        continue;
                    }

                    for (const FaceDef& def : kFaces) {
                        // The neighbourhood answers uniformly whether the neighbour
                        // is inside this section or across a boundary, so there is
                        // no in-section special case left. Coordinates outside the
                        // 3x3x3 block, and sections that are not loaded, read as air.
                        const BlockId neighbour =
                            hood.blockAt(x + def.dx, y + def.dy, z + def.dz);
                        const bool hidden = blocks.isOpaque(neighbour)
                                         || (fluidPass && blocks.isFluid(neighbour));
                        if (hidden) {
                            continue;
                        }

                        // A texture array layer, not the BlockId. Both meshers must
                        // agree on what the material field means, or a mesh from
                        // this one would be textured wrongly -- and it stays
                        // renderable precisely so it can be compared against the
                        // greedy mesher on screen, not just in tests.
                        out.quads.push_back(
                            Quad::make(static_cast<u32>(x + def.ox),
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
}

void meshSectionCulled(const Section& section,
                       ChunkMesh& out,
                       const MeshOptions& options) {
    SectionNeighbourhood hood;
    hood.set(0, 0, 0, &section);
    meshSectionCulled(hood, out, options);
}

} // namespace mc
