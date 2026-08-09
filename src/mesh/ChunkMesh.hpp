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

    bool empty() const noexcept { return quads.empty(); }
    usize quadCount() const noexcept { return quads.size(); }
    usize byteSize() const noexcept { return quads.size() * sizeof(Quad); }

    void clear() { quads.clear(); }
};

} // namespace mc
