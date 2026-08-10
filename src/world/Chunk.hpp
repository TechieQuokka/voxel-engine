#pragma once

#include "world/Coords.hpp"
#include "world/Section.hpp"

#include <array>
#include <atomic>

namespace mc {

/// Where a column is in the streaming lifecycle.
///
/// Read by the main thread and written by workers, so `Chunk::state` is atomic.
/// `Generating` is the state that matters for safety: it means a job owns the
/// column's sections, and neither the renderer nor the unloader may touch it.
enum class ChunkState : u32 {
    Empty = 0,  ///< In the map, no voxels yet.
    Generating, ///< A generation job owns it.
    Ready,      ///< Voxels present and readable.
};

/// A vertical column of 12 sections — the unit the World loads and unloads.
///
/// A column rather than a cube because terrain generation is column-shaped: the
/// 2D inputs of DESIGN.md 3.12 (continentalness, erosion, peaks and valleys) are
/// evaluated once per column and then shared down all 12 sections. Splitting that
/// across independently loaded cubes would either duplicate the work or need a
/// cache between them.
class Chunk {
public:
    static constexpr usize kSectionCount = static_cast<usize>(kSectionsPerColumn);

    explicit Chunk(ChunkPos position) : m_position(position) {}

    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    ChunkPos position() const noexcept { return m_position; }

    ChunkState state() const noexcept { return m_state.load(std::memory_order_acquire); }
    void setState(ChunkState state) noexcept {
        m_state.store(state, std::memory_order_release);
    }

    Section& sectionByIndex(usize index) {
        MC_ASSERT(index < kSectionCount);
        return m_sections[index];
    }
    const Section& sectionByIndex(usize index) const {
        MC_ASSERT(index < kSectionCount);
        return m_sections[index];
    }

    /// Null when `sectionY` falls outside the world's vertical range. Callers get
    /// a null rather than an assertion because neighbour lookups legitimately ask
    /// about the sections above the sky and below bedrock.
    Section* sectionAt(i32 sectionY);
    const Section* sectionAt(i32 sectionY) const;

    /// Mesh invalidation, one bit per section.
    ///
    /// A bitmask rather than 12 flags so that "does this column need any work?"
    /// is a single atomic load — the streaming scheduler asks that of every
    /// loaded column every frame.
    void markSectionDirty(usize index);
    void clearSectionDirty(usize index);
    bool isSectionDirty(usize index) const;
    u16 dirtyMask() const noexcept { return m_dirty.load(std::memory_order_acquire); }
    bool anyDirty() const noexcept { return dirtyMask() != 0; }

    /// Marks every section dirty. Used when a column finishes generating, and
    /// when a neighbour appears and the boundary faces have to be reconsidered.
    void markAllDirty();

    usize memoryUsage() const;

private:
    ChunkPos m_position;
    /// Default-initialized rather than `{}`: Section's default constructor is
    /// explicit, which list-initialization of the array elements may not use.
    /// Each section therefore starts as uniform air, costing no index array.
    std::array<Section, kSectionCount> m_sections;
    std::atomic<ChunkState> m_state{ChunkState::Empty};
    std::atomic<u16> m_dirty{0};
};

static_assert(Chunk::kSectionCount <= 16, "the dirty mask is 16 bits wide");

} // namespace mc
