#pragma once

#include "mesh/ChunkMesh.hpp"
#include "world/Neighbourhood.hpp"
#include "world/Section.hpp"

namespace mc {

struct MeshOptions {
    /// Placeholder. Kept so the signature does not have to change again when the
    /// reference mesher grows an option worth having.
    bool unused = false;
};

/// Straightforward face culling: emit one 1x1 quad per visible block face.
///
/// This is the reference implementation, not the fast one. Phase 2 replaced it
/// with binary greedy meshing; keeping this around gives that work an oracle to
/// diff against, since both must produce the same visible surface -- including at
/// section boundaries, which is why this takes a neighbourhood too.
void meshSectionCulled(const SectionNeighbourhood& hood,
                       ChunkMesh& out,
                       const MeshOptions& options = {});

/// Meshes a section in isolation, with everything around it treated as air.
void meshSectionCulled(const Section& section,
                       ChunkMesh& out,
                       const MeshOptions& options = {});

} // namespace mc
