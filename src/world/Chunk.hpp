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

    /// Whether this column differs from what the generator would produce for it.
    ///
    /// **This is what decides whether the column is written to disk**, and it is the
    /// whole reason a save grows with what the player did rather than with where
    /// they went. Generation is deterministic, so an untouched column is reproduced
    /// exactly by generating it again; writing it out as well would put tens of
    /// thousands of identical columns on disk after one long flight.
    ///
    /// Set by `World::setBlock`, and by loading: a column that came off disk is
    /// written back on unload because a furnace standing in it may have burned down
    /// in the meantime, and nothing else would notice that its timers moved.
    ///
    /// Plain rather than atomic, unlike the three above it. Those are written by
    /// workers and read by the main thread; this one is only ever touched on the
    /// main thread -- editing, loading and unloading all happen there.
    bool edited() const noexcept { return m_edited; }
    void markEdited() noexcept { m_edited = true; }

    /// Keeps the column alive while something outside holds pointers into it.
    ///
    /// A meshing job borrows `const Section*` from up to nine columns -- its own and
    /// its eight horizontal neighbours -- and holds them across frames. If the camera
    /// moves far enough in the meantime, the World would unload one of those columns
    /// and the job would read freed memory. A pin says "not yet".
    ///
    /// A counter rather than a flag, because one column is a neighbour of nine
    /// sections and can therefore be pinned by nine jobs at once. The state enum
    /// covers the other direction: a column being *written* is Generating.
    void pin() noexcept { m_pins.fetch_add(1, std::memory_order_acquire); }
    void unpin() noexcept {
        const u32 previous = m_pins.fetch_sub(1, std::memory_order_release);
        MC_ASSERT_MSG(previous > 0, "unpinned a column that was not pinned");
        (void)previous;
    }
    bool pinned() const noexcept { return m_pins.load(std::memory_order_acquire) != 0; }

    /// Whether any block in this column emits light.
    ///
    /// **A cached answer to a question the streaming path would otherwise ask by
    /// scanning.** Every column that arrives has to be relit, and relighting reads
    /// the nine columns around it -- so without this the main thread walks a hundred
    /// and eight section palettes per column streamed in, to discover that a world
    /// nobody has put a torch in has no torches in it. Set by the worker that filled
    /// the column, which is the thread that has just touched every voxel anyway.
    ///
    /// Atomic for the same reason `m_state` is: written by a worker, read by the
    /// main thread. Conservative in one direction only -- it goes true when a torch
    /// is placed and is never cleared, so at worst it costs a flood that finds
    /// nothing. A stale false would lose light, and nothing sets it false.
    bool hasEmitter() const noexcept {
        return m_hasEmitter.load(std::memory_order_acquire);
    }
    void markHasEmitter() noexcept {
        m_hasEmitter.store(true, std::memory_order_release);
    }

    usize memoryUsage() const;

private:
    ChunkPos m_position;
    /// Default-initialized rather than `{}`: Section's default constructor is
    /// explicit, which list-initialization of the array elements may not use.
    /// Each section therefore starts as uniform air, costing no index array.
    std::array<Section, kSectionCount> m_sections;
    std::atomic<ChunkState> m_state{ChunkState::Empty};
    std::atomic<u16> m_dirty{0};
    std::atomic<u32> m_pins{0};
    std::atomic<bool> m_hasEmitter{false};
    bool m_edited = false;
};

static_assert(Chunk::kSectionCount <= 16, "the dirty mask is 16 bits wide");

} // namespace mc
