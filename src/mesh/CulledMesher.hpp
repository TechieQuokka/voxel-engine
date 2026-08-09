#pragma once

#include "mesh/ChunkMesh.hpp"
#include "world/Section.hpp"

namespace mc {

struct MeshOptions {
    /// Emit faces on the section boundary. Correct while a section is rendered
    /// in isolation; once the World supplies neighbours in Phase 3 these faces
    /// are culled against the adjacent section instead.
    bool emitBoundaryFaces = true;
};

/// Straightforward face culling: emit one 1x1 quad per visible block face.
///
/// This is the reference implementation, not the fast one. Phase 2 replaces it
/// with binary greedy meshing; keeping this around gives that work an oracle to
/// diff against, since both must produce the same visible surface.
void meshSectionCulled(const Section& section, ChunkMesh& out, const MeshOptions& options = {});

} // namespace mc
