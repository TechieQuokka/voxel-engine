#pragma once

#include "mesh/ChunkMesh.hpp"
#include "world/Neighbourhood.hpp"
#include "world/Section.hpp"

namespace mc {

struct GreedyMeshOptions {
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

/// Binary greedy meshing over a section and its neighbours.
///
/// Face culling is done with bitwise operations over 32-bit occupancy columns,
/// which resolves a whole axis of 32 voxels per instruction instead of
/// per-voxel neighbour lookups. Visible faces are then merged into the largest
/// rectangles that share a material (and, optionally, an AO pattern).
///
/// Only the centre section's faces are emitted. The neighbours are read, never
/// meshed: a face belongs to the solid voxel behind it, and that voxel's own
/// section is responsible for it.
///
/// `hood` must have a centre. Null neighbours read as air, which is correct both
/// for the sky and for a column that has not streamed in yet -- the latter gets
/// remeshed when it arrives, because the World marks its neighbours dirty.
///
/// Must produce the same visible surface as meshSectionCulled -- the tests
/// assert that the merged quad areas equal the unmerged quad count.
void meshSectionGreedy(const SectionNeighbourhood& hood,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options = {});

/// Meshes a section in isolation, with everything around it treated as air.
///
/// For tests and for any single-section scene. Equivalent to passing a
/// neighbourhood whose only entry is the centre.
void meshSectionGreedy(const Section& section,
                       ChunkMesh& out,
                       const GreedyMeshOptions& options = {});

} // namespace mc
