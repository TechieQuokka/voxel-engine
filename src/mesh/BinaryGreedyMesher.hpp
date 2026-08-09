#pragma once

#include "mesh/ChunkMesh.hpp"
#include "world/Section.hpp"

namespace mc {

struct GreedyMeshOptions {
    /// Emit faces on the section boundary. Correct while a section is rendered
    /// in isolation; the World supplies neighbours from Phase 3.
    bool emitBoundaryFaces = true;

    /// Compute per-corner ambient occlusion.
    bool ambientOcclusion = true;

    /// Merge only cells whose AO pattern is identical.
    ///
    /// This is the setting the whole Phase 2 measurement is about. AO differs
    /// between adjacent faces near any edge, so requiring it to match blocks
    /// most merges -- which is why Minecraft does not use greedy meshing at
    /// all. Disabling it maximises merging at the cost of AO being wrong on
    /// merged quads, which is the correct trade for distant LOD levels where
    /// AO is not perceptible.
    bool aoAwareMerging = true;
};

/// Binary greedy meshing.
///
/// Face culling is done with bitwise operations over 32-bit occupancy columns,
/// which resolves a whole axis of 32 voxels per instruction instead of
/// per-voxel neighbour lookups. Visible faces are then merged into the largest
/// rectangles that share a material (and, optionally, an AO pattern).
///
/// Must produce the same visible surface as meshSectionCulled -- the tests
/// assert that the merged quad areas equal the unmerged quad count.
void meshSectionGreedy(const Section& section,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options = {});

} // namespace mc
