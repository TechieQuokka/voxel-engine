#pragma once

#include "world/BlockLight.hpp"
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
        /// Columns that fell outside the region but were left alone because a job
        /// still owns them. They get dropped on a later call.
        usize retained = 0;

        /// The columns that went away, still alive.
        ///
        /// **Handed over rather than destroyed, because unloading is when an edited
        /// column has to be written to disk.** The World does not know about the
        /// save -- it holds no store and no path -- so it drops ownership here and
        /// lets the caller decide. They die when this result does.
        ///
        /// The renderer needs their positions too, to release their GPU meshes and
        /// to remesh the survivors next door whose boundary faces were culled
        /// against them. `Chunk::position()` answers that.
        std::vector<std::unique_ptr<Chunk>> unloaded;
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

    /// kAirBlock outside the world's vertical range, outside loaded columns, and in
    /// any column that has not finished generating.
    ///
    /// **That last case is a thread-safety contract, not a convenience.** This is the
    /// one voxel read the main thread makes while workers are writing, and a column
    /// in `Generating` is being filled by one of them. Reading it would race
    /// `Palette::fill`, which frees the index vector -- so the answer is air until
    /// the generator's release store makes the column Ready.
    BlockId blockAt(BlockPos pos) const noexcept;

    /// Whether `blockAt(pos)` is answering from real voxels rather than from the
    /// "not here" fallback above.
    ///
    /// **This is the test that sideways spread needs and downward spread does not.**
    /// `blockAt` conflates three different situations into `kAirBlock`: genuine air,
    /// a column outside the loaded region, and a column a worker is still filling.
    /// Every caller so far only ever read *down*, into the same column it was already
    /// standing in, where that conflation is harmless -- `BlockUpdates::examine` has
    /// the argument written out at the read.
    ///
    /// Flowing water reads its four horizontal neighbours, which are in up to four
    /// different columns, and "air" there would mean water pouring off the edge of
    /// the loaded region into a chunk that has simply not arrived. Vanilla answers
    /// this by refusing rather than solving it: fluid spreads into the first block of
    /// a non-ticking chunk and then suspends until that chunk loads. This is the
    /// query that lets the same thing happen here, with the update re-queued rather
    /// than dropped.
    bool isReadyAt(BlockPos pos) const noexcept;

    /// Why an edit did or did not happen. Anything but `Applied` left the world
    /// untouched.
    enum class EditStatus {
        Applied,
        /// That block was already there. Not an error, and not worth remeshing for.
        Unchanged,
        /// Outside the world's vertical range.
        OutsideWorld,
        /// The column is not loaded, or has not finished generating.
        NotLoaded,
        /// A job owns the column right now. **The caller should retry next frame**,
        /// not give up: this is a timing collision, not a refusal.
        Busy,
    };

    /// Replaces one block, and marks everything whose mesh that invalidates.
    ///
    /// Main thread only, and it is the pin that makes that safe rather than a lock.
    /// A meshing job borrows `const Section*` into nine columns and holds them
    /// across frames; `Palette::set` can reallocate the index array when the palette
    /// outgrows its bit width, so writing under a reader is a use-after-free rather
    /// than a torn read. Every reader of this column pins it -- jobs for neighbouring
    /// sections pin all nine of theirs -- so one `pinned()` test covers them all, and
    /// `Busy` asks the caller to come back. Pins sit near zero in a steady state, so
    /// in practice this retries at most a frame or two.
    ///
    /// Three things get dirtied, and the third is the one that is easy to miss:
    ///
    /// 1. The section holding the block.
    /// 2. Sections the block *touches* -- the mesher's AO reads a 3x3x3 of voxels, so
    ///    a block in a section corner changes shading in up to seven neighbours.
    /// 3. Wherever the sky light moved. Light is in the mesher's merge key, and the
    ///    padded light grid reaches one voxel into the adjacent column, so a light
    ///    change has to dirty the same section in the eight surrounding columns too.
    ///    `computeSkyLight` reports which sections moved, which is what keeps this
    ///    from being "remesh the neighbourhood on every click" -- underground, where
    ///    digging mostly happens, no sky light changes and nothing here fires.
    EditStatus setBlock(BlockPos pos, BlockId block);

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

    /// Columns held past their welcome, by reason. Both should sit near zero in a
    /// steady state; a number that only grows is a leaked pin or a lost job, and the
    /// symptom is a loaded set larger than the render distance asks for.
    struct HeldCounts {
        usize pinned = 0;
        usize generating = 0;
        usize outsideRegion = 0;
    };
    HeldCounts heldCounts(ChunkPos center) const;

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

    /// Marks every section that has to be remeshed because block light moved in one
    /// of `touched`, which is each of them plus the twenty-six around it.
    ///
    /// Public because relighting a column that has just loaded happens outside an
    /// edit -- `Engine` seeds it and then has to say what changed.
    void dirtyAround(const LightTouch& touched);

private:
    using ChunkMap = std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash>;

    i32 m_renderDistance;
    ChunkMap m_chunks;
};

} // namespace mc
