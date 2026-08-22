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

    /// The layout is **opaque, then model, then cutout, then translucent**, in that
    /// order, and these three counts are where the joins fall.
    ///
    /// **One list with split points rather than four lists**, because the arena
    /// allocates one range per section and four lists would mean four allocations,
    /// four placements and four lifetimes to keep in step. Three `usize`s let the
    /// renderer issue four draws over the same contiguous range, each simply
    /// starting partway in.
    ///
    /// The order is not arbitrary: opaque fills the depth buffer, model adds more
    /// depth-writing geometry to it, cutout tests against both and may discard, and
    /// translucent blends over everything and writes no depth. Drawing them out of
    /// order is drawing them wrong.
    usize opaqueQuads = 0;

    /// Non-cube geometry -- slabs, and later stairs, doors and fences.
    ///
    /// **These are `ModelBox` words, not `Quad`s, sharing the arena and the element
    /// type.** See mesh/ModelBox.hpp for why they are the same 64 bits and for what
    /// that costs. They are counted in *boxes*, and each box draws 36 vertices where a
    /// quad draws 6 -- so this number is not interchangeable with the others.
    usize modelBoxes = 0;

    /// Glass. Alpha-tested, depth-written, drawn after opaque -- see
    /// `BlockInfo::cutout` for why it is not simply part of the opaque pass.
    usize cutoutQuads = 0;

    bool empty() const noexcept { return quads.empty(); }
    usize quadCount() const noexcept { return quads.size(); }
    usize translucentQuads() const noexcept {
        return quads.size() - opaqueQuads - modelBoxes - cutoutQuads;
    }
    usize byteSize() const noexcept { return quads.size() * sizeof(Quad); }

    void clear() {
        quads.clear();
        opaqueQuads = 0;
        modelBoxes = 0;
        cutoutQuads = 0;
    }
};

} // namespace mc
