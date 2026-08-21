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

    /// The layout is **opaque, then cutout, then translucent**, in that order, and
    /// these two counts are where the joins fall.
    ///
    /// **One list with split points rather than three lists**, because the arena
    /// allocates one range per section and three lists would mean three allocations,
    /// three placements and three lifetimes to keep in step. Two `usize`s let the
    /// renderer issue three draws over the same contiguous range, each simply
    /// starting partway in.
    ///
    /// The order is not arbitrary: opaque fills the depth buffer, cutout tests
    /// against it and may discard, and translucent blends over both and writes no
    /// depth. Drawing them out of order is drawing them wrong.
    usize opaqueQuads = 0;

    /// Glass. Alpha-tested, depth-written, drawn after opaque -- see
    /// `BlockInfo::cutout` for why it is not simply part of the opaque pass.
    usize cutoutQuads = 0;

    bool empty() const noexcept { return quads.empty(); }
    usize quadCount() const noexcept { return quads.size(); }
    usize translucentQuads() const noexcept {
        return quads.size() - opaqueQuads - cutoutQuads;
    }
    usize byteSize() const noexcept { return quads.size() * sizeof(Quad); }

    void clear() {
        quads.clear();
        opaqueQuads = 0;
        cutoutQuads = 0;
    }
};

} // namespace mc
