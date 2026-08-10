#pragma once

#include "world/Chunk.hpp"
#include "world/Neighbourhood.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace mc {

/// The loaded set of chunk columns, and the streaming policy that maintains it.
///
/// Columns are held by `unique_ptr` rather than by value. That is not a style
/// preference: generation and meshing jobs hold a `Chunk*` across frames, and a
/// value-holding map would invalidate every one of them on rehash.
class World {
public:
    /// `renderDistance` is a square radius in columns, so distance 16 loads a
    /// 33x33 region. Square rather than circular to match how the streaming
    /// region is indexed and how the frustum test is done per column.
    explicit World(i32 renderDistance);

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    i32 renderDistance() const noexcept { return m_renderDistance; }
    void setRenderDistance(i32 chunks);

    struct LoadResult {
        usize created = 0;
        usize unloaded = 0;
        /// Columns that fell outside the region but were left alone because a job
        /// still owns them. They get dropped on a later call.
        usize retained = 0;
    };

    /// Brings the loaded set in line with a square around `center`: creates what is
    /// missing, drops what fell outside.
    ///
    /// A column in `ChunkState::Generating` is never dropped, because a worker is
    /// writing into it. It is simply skipped and reconsidered next call.
    LoadResult updateLoadedRegion(ChunkPos center);

    /// Drops every column, regardless of state. Only safe once no jobs are in
    /// flight.
    void clear();

    Chunk* find(ChunkPos pos) noexcept;
    const Chunk* find(ChunkPos pos) const noexcept;

    /// Null when the column is not loaded or `pos.y` is outside the world.
    const Section* sectionAt(SectionPos pos) const noexcept;

    /// kAirBlock outside the world's vertical range and outside loaded columns.
    BlockId blockAt(BlockPos pos) const noexcept;

    /// Gathers the 3x3x3 sections around `center` for a meshing job.
    ///
    /// Entries are null where the column is not loaded or the section Y is out of
    /// range, which the mesher reads as air. See SectionNeighbourhood for why the
    /// full 27 are needed rather than the 6 face-adjacent ones.
    SectionNeighbourhood neighbourhood(SectionPos center) const;

    /// True when `pos` lies inside the current render distance around `center`.
    bool isInRegion(ChunkPos pos, ChunkPos center) const noexcept;

    usize loadedChunkCount() const noexcept { return m_chunks.size(); }
    usize memoryUsage() const;

    /// Visits every loaded column. Used by the streaming scheduler and, in 3d, by
    /// the renderer.
    template <typename F>
    void forEachChunk(F&& fn) {
        for (auto& [pos, chunk] : m_chunks) {
            fn(*chunk);
        }
    }

    template <typename F>
    void forEachChunk(F&& fn) const {
        for (const auto& [pos, chunk] : m_chunks) {
            fn(*chunk);
        }
    }

private:
    using ChunkMap = std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash>;

    i32 m_renderDistance;
    ChunkMap m_chunks;
};

} // namespace mc
