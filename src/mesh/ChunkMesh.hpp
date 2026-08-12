#pragma once

#include "core/Types.hpp"
#include "mesh/Quad.hpp"

#include <vector>

namespace mc {

/// CPU-side mesh for one section: a flat list of packed quads.
///
/// Deliberately just a vector. Meshing runs on worker threads and hands the
/// result to the upload thread, so the mesh must be cheap to move and carry no
/// GPU state of its own.
struct ChunkMesh {
    std::vector<Quad> quads;

    /// How many of `quads` are opaque. The rest are translucent (water) and follow.
    ///
    /// **One list with a split rather than two lists**, because the arena allocates
    /// one range per section and two lists would mean two allocations, two
    /// placements and two lifetimes to keep in step. A split point costs a `usize`
    /// and lets the renderer issue the two passes as two draws over the same
    /// contiguous range -- the translucent one simply starts partway in.
    usize opaqueQuads = 0;

    bool empty() const noexcept { return quads.empty(); }
    usize quadCount() const noexcept { return quads.size(); }
    usize translucentQuads() const noexcept { return quads.size() - opaqueQuads; }
    usize byteSize() const noexcept { return quads.size() * sizeof(Quad); }

    void clear() {
        quads.clear();
        opaqueQuads = 0;
    }
};

} // namespace mc
