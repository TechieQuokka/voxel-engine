#pragma once

#include "world/Coords.hpp"
#include "world/Section.hpp"

#include <array>

namespace mc {

/// The 3x3x3 block of sections centred on one section, as borrowed pointers.
///
/// **Why 27 and not 6.** Face culling only needs the six face-adjacent sections:
/// a face exists where a solid voxel meets a non-solid one across a boundary.
/// Ambient occlusion needs more. Each corner of a face is darkened by its two
/// edge neighbours *and the diagonal one*, so a voxel on a section corner samples
/// positions that are edge- and corner-adjacent, not merely face-adjacent. Culling
/// boundary faces correctly while leaving AO to guess produces a bright rim around
/// every chunk — which is worse than the redundant walls it fixed, because it is
/// visible from anywhere rather than only in a wireframe.
///
/// Null entries mean "not loaded, or outside the world's vertical range", and read
/// as air. That is the right default in both cases: an unloaded neighbour is
/// remeshed anyway once it arrives (the column is marked dirty), and the sky
/// really is air.
///
/// Pointers are borrowed. The neighbourhood must not outlive the meshing job that
/// holds it, and the World must not unload a column while one is in flight.
class SectionNeighbourhood {
public:
    static constexpr usize kCount = 27;

    /// `dx`, `dy`, `dz` are each in [-1, 1].
    static constexpr usize indexOf(i32 dx, i32 dy, i32 dz) {
        MC_ASSERT(dx >= -1 && dx <= 1);
        MC_ASSERT(dy >= -1 && dy <= 1);
        MC_ASSERT(dz >= -1 && dz <= 1);
        return static_cast<usize>((dz + 1) * 9 + (dy + 1) * 3 + (dx + 1));
    }

    void set(i32 dx, i32 dy, i32 dz, const Section* section) {
        m_sections[indexOf(dx, dy, dz)] = section;
    }

    const Section* at(i32 dx, i32 dy, i32 dz) const {
        return m_sections[indexOf(dx, dy, dz)];
    }

    const Section* center() const { return m_sections[indexOf(0, 0, 0)]; }

    /// Block at a coordinate relative to the centre section's own origin.
    ///
    /// Coordinates in [0, 32) resolve inside the centre; -1 and 32 resolve into the
    /// adjacent section. Anything further away, or in a section that is not loaded,
    /// reads as air. The mesher only ever asks for [-1, 32].
    BlockId blockAt(i32 x, i32 y, i32 z) const {
        // Arithmetic shift, so -1 floors to section offset -1 rather than to 0.
        const i32 dx = blockToSectionCoord(x);
        const i32 dy = blockToSectionCoord(y);
        const i32 dz = blockToSectionCoord(z);

        if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || dz < -1 || dz > 1) {
            return kAirBlock;
        }

        const Section* section = at(dx, dy, dz);
        if (section == nullptr) {
            return kAirBlock;
        }
        return section->get(blockToLocalCoord(x), blockToLocalCoord(y), blockToLocalCoord(z));
    }

private:
    std::array<const Section*, kCount> m_sections{};
};

} // namespace mc
