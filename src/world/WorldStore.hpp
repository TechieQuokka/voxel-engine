#pragma once

#include "core/Result.hpp"
#include "core/Types.hpp"
#include "world/ChunkCodec.hpp"
#include "world/Coords.hpp"
#include "world/RegionFile.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace mc {

class Chunk;

/// A directory of region files, and the world identity that ties them to a seed.
///
/// ## What is saved, and what is not
///
/// **Only columns the player edited.** Generation is deterministic -- `Generator` is
/// const, reads nothing but a shared noise graph and writes nothing but its own
/// column -- so a column nobody touched is reproduced exactly by generating it
/// again. Saving it as well would make the world on disk grow with where the player
/// *walked* rather than with what they *did*: a twenty-minute flight at render
/// distance 16 touches tens of thousands of columns and changes none of them.
///
/// **The seed is written once and checked on every open**, because the whole scheme
/// rests on the unedited columns regenerating identically. Opening a save against a
/// different seed would drop edited columns into terrain that was never theirs,
/// which looks like corruption and is not detectable after the fact. It is detected
/// here instead, before anything is read.
///
/// Determinism holds within a machine. FastNoise2 dispatches on the widest SIMD the
/// CPU has, and a save carried to a different one could in principle regenerate its
/// *unedited* columns slightly differently; the edited ones, which are the only ones
/// on disk, are unaffected. This project is one Linux target by constraint
/// (DESIGN.md 1), so that is recorded rather than guarded against.
///
/// ## Threading
///
/// **One lock over the whole store, and the generation workers are what it is for.**
/// Loading happens inside the generation job -- that is the one place a column is
/// owned by a single thread and not yet visible to anything else -- so up to six
/// workers reach this at once, and two of them wanting neighbouring columns are
/// wanting the same region file's sector table.
///
/// The lock covers the file, not the codec: bytes are read under it and decoded
/// after it is released, because decoding recomputes a column's sky light and that
/// is the expensive half.
class WorldStore {
public:
    enum class Error {
        /// The directory or a file in it could not be opened or created.
        CannotOpen,
        /// A read or write failed.
        Io,
        /// A region or the level file is not usable.
        Corrupt,
        /// This save belongs to a different world.
        SeedMismatch,
    };

    /// Opens `directory`, creating it and its level file if they do not exist.
    ///
    /// Fails with `SeedMismatch` when a level file is already there for a different
    /// seed, rather than writing edits into someone else's world.
    static Result<std::unique_ptr<WorldStore>, Error> open(
        const std::filesystem::path& directory, u32 seed);

    WorldStore(const WorldStore&) = delete;
    WorldStore& operator=(const WorldStore&) = delete;
    ~WorldStore();

    /// Fills `chunk` from disk if it was saved, and reports whether it was.
    ///
    /// A `false` return is the ordinary case, not a failure: it means nobody has
    /// edited this column and the caller should generate it. An `Error` return means
    /// the save is there and unreadable, and the caller should *also* generate --
    /// see `Generator::generateColumn`, every branch of which refills the section it
    /// writes, so a half-decoded column is overwritten rather than patched.
    ///
    /// Does not touch the column's state or dirty mask. The caller sets `Ready` and
    /// marks dirty, so that a loaded column and a generated one arrive the same way.
    Result<bool, Error> loadColumn(Chunk& chunk, std::vector<SavedFurnace>* furnaces);

    /// Writes a column and the furnaces standing in it.
    Result<void, Error> saveColumn(const Chunk& chunk,
                                   std::span<const SavedFurnace> furnaces = {});

    /// Flushes every open region. Called on shutdown.
    void flush();

    /// Counters for the stats line.
    ///
    /// **`failed` exists because of this project's oldest lesson**: item pickup was
    /// broken for four sessions because nothing printed a number that would have
    /// been zero. A save that silently stops writing looks exactly like a save that
    /// has nothing to write.
    struct Stats {
        usize columnsLoaded = 0;
        usize columnsSaved = 0;
        usize failures = 0;
    };
    Stats stats() const;

    /// Where this store keeps its files.
    const std::filesystem::path& directory() const noexcept { return m_directory; }

private:
    WorldStore() = default;

    /// The region a column belongs to, and its position inside that region.
    struct Address {
        ChunkPos region;
        i32 localX;
        i32 localZ;
    };
    static Address addressOf(ChunkPos pos);

    std::filesystem::path pathFor(ChunkPos region) const;

    /// The open region for `region`, opening it if it is not already.
    ///
    /// A null value with no error means the region has never been written and
    /// `createIfMissing` was false -- the ordinary answer for a column nobody
    /// edited, and the reason reading never leaves a file behind. Call with the lock
    /// held.
    Result<RegionFile*, Error> regionFor(ChunkPos region, bool createIfMissing);

    /// Closes the least recently used region when too many are open. Call with the
    /// lock held.
    void evictIfNeeded();

    /// How many region files stay open at once.
    ///
    /// A loaded set at render distance 16 spans two regions on each axis, so four is
    /// the working set and the rest of this is slack for a player flying across a
    /// boundary. The cost of a wrong answer here is an open() and a header read, not
    /// a correctness problem.
    static constexpr usize kMaxOpenRegions = 16;

    struct OpenRegion {
        std::unique_ptr<RegionFile> file;
        /// Monotonic counter, for the eviction order. Not a clock: two regions
        /// touched in the same millisecond still have to be ordered.
        u64 lastUsed = 0;
    };

    std::filesystem::path m_directory;
    u32 m_seed = 0;

    mutable std::mutex m_mutex;
    std::unordered_map<ChunkPos, OpenRegion, ChunkPosHash> m_regions;
    u64 m_useCounter = 0;
    Stats m_stats;
};

const char* describe(WorldStore::Error error);

} // namespace mc
