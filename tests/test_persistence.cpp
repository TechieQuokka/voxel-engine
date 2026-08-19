#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCodec.hpp"
#include "world/Furnace.hpp"
#include "world/RegionFile.hpp"
#include "world/Smelting.hpp"
#include "world/WorldStore.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace mc;

namespace {

/// A directory that exists for the life of one test case and is removed after it.
///
/// The tests below are the only ones in this suite that touch the filesystem, so
/// they clean up after themselves rather than leaving a fixture behind -- a stale
/// save from a previous run is exactly the input that would make the next one pass
/// for the wrong reason.
class TempDir {
public:
    TempDir() {
        static std::atomic<u32> counter{0};
        m_path = std::filesystem::temp_directory_path()
               / ("mc_persistence_test_" + std::to_string(::getpid()) + "_"
                  + std::to_string(counter.fetch_add(1)));
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempDir() {
        std::error_code code;
        std::filesystem::remove_all(m_path, code);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

/// Fills a column with a recognisable pattern, keyed off its position so two
/// columns never hold the same thing.
void fillColumn(Chunk& chunk, BlockId marker) {
    chunk.sectionByIndex(0).fill(kBedrockBlock);
    chunk.sectionByIndex(1).fill(kStoneBlock);
    chunk.sectionByIndex(2).set(1, 2, 3, marker);
    chunk.sectionByIndex(2).set(31, 31, 31, marker);
}

std::vector<u8> payloadOf(usize size, u8 seed) {
    std::vector<u8> bytes(size);
    for (usize i = 0; i < size; ++i) {
        bytes[i] = static_cast<u8>((i * 31 + seed) & 0xFF);
    }
    return bytes;
}

} // namespace

// -- RegionFile -----------------------------------------------------------------

TEST_CASE("a fresh region reports every column absent") {
    const TempDir dir;
    auto region = RegionFile::open(dir.path() / "r.0.0.mcr", true);
    REQUIRE(region.hasValue());

    auto read = region.value()->read(0, 0);
    REQUIRE(read.hasValue());
    CHECK_FALSE(read.value().has_value());
    CHECK(region.value()->storedCount() == 0);
}

TEST_CASE("a payload written to a region comes back byte for byte") {
    const TempDir dir;
    auto region = RegionFile::open(dir.path() / "r.0.0.mcr", true);
    REQUIRE(region.hasValue());

    const std::vector<u8> payload = payloadOf(5000, 7);
    REQUIRE(region.value()->write(3, 17, payload).hasValue());

    auto read = region.value()->read(3, 17);
    REQUIRE(read.hasValue());
    REQUIRE(read.value().has_value());
    CHECK(*read.value() == payload);
    CHECK(region.value()->storedCount() == 1);

    // And the neighbours are still absent, so the slot arithmetic is not aliasing.
    auto other = region.value()->read(17, 3);
    REQUIRE(other.hasValue());
    CHECK_FALSE(other.value().has_value());
}

TEST_CASE("a region survives being closed and reopened") {
    const TempDir dir;
    const std::filesystem::path path = dir.path() / "r.-2.5.mcr";
    const std::vector<u8> payload = payloadOf(9000, 3);

    {
        auto region = RegionFile::open(path, true);
        REQUIRE(region.hasValue());
        REQUIRE(region.value()->write(31, 0, payload).hasValue());
        REQUIRE(region.value()->flush().hasValue());
    }

    auto reopened = RegionFile::open(path, true);
    REQUIRE(reopened.hasValue());
    auto read = reopened.value()->read(31, 0);
    REQUIRE(read.hasValue());
    REQUIRE(read.value().has_value());
    CHECK(*read.value() == payload);
}

TEST_CASE("a column that outgrows its sectors is relocated, not truncated") {
    // The case that breaks a naive region file: a player keeps digging in one
    // column, its palette widens, and the payload no longer fits where it sat.
    const TempDir dir;
    auto region = RegionFile::open(dir.path() / "r.0.0.mcr", true);
    REQUIRE(region.hasValue());

    const std::vector<u8> small = payloadOf(100, 1);
    const std::vector<u8> large = payloadOf(20000, 2);

    REQUIRE(region.value()->write(5, 5, small).hasValue());
    REQUIRE(region.value()->write(6, 5, payloadOf(100, 9)).hasValue()); // a neighbour, right after it
    REQUIRE(region.value()->write(5, 5, large).hasValue());

    auto grown = region.value()->read(5, 5);
    REQUIRE(grown.hasValue());
    REQUIRE(grown.value().has_value());
    CHECK(*grown.value() == large);

    // The neighbour must not have been written over by the relocation.
    auto neighbour = region.value()->read(6, 5);
    REQUIRE(neighbour.hasValue());
    REQUIRE(neighbour.value().has_value());
    CHECK(*neighbour.value() == payloadOf(100, 9));
}

TEST_CASE("a column that shrinks gives its spare sectors back") {
    // Without this the file grows on every save of a column that got simpler,
    // which is what happens when a player fills a hole back in.
    const TempDir dir;
    const std::filesystem::path path = dir.path() / "r.0.0.mcr";

    auto region = RegionFile::open(path, true);
    REQUIRE(region.hasValue());
    REQUIRE(region.value()->write(0, 0, payloadOf(40000, 1)).hasValue());
    REQUIRE(region.value()->write(0, 0, payloadOf(100, 2)).hasValue());
    // The next column should land in the sectors the first one gave up, so the
    // file does not grow past what the large payload once needed.
    REQUIRE(region.value()->write(1, 0, payloadOf(20000, 3)).hasValue());
    REQUIRE(region.value()->flush().hasValue());

    const auto size = std::filesystem::file_size(path);
    CHECK(size <= 48u * 1024u);

    auto first = region.value()->read(0, 0);
    REQUIRE(first.hasValue());
    CHECK(*first.value() == payloadOf(100, 2));
    auto second = region.value()->read(1, 0);
    REQUIRE(second.hasValue());
    CHECK(*second.value() == payloadOf(20000, 3));
}

TEST_CASE("a region whose header claims sectors past the end is refused") {
    const TempDir dir;
    const std::filesystem::path path = dir.path() / "r.0.0.mcr";

    {
        auto region = RegionFile::open(path, true);
        REQUIRE(region.hasValue());
        REQUIRE(region.value()->write(0, 0, payloadOf(100, 1)).hasValue());
        REQUIRE(region.value()->flush().hasValue());
    }

    // Point slot 0 at sector 9000, far past the end of a file this size.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(file.is_open());
        const u32 entry = (9000u << 8) | 1u;
        file.seekp(0, std::ios::beg);
        file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }

    auto reopened = RegionFile::open(path, true);
    REQUIRE_FALSE(reopened.hasValue());
    CHECK(reopened.error() == RegionFile::Error::Corrupt);
}

// -- WorldStore -----------------------------------------------------------------

TEST_CASE("a store returns false for a column nobody edited") {
    const TempDir dir;
    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());

    Chunk chunk(ChunkPos{4, -9});
    const Result<bool, WorldStore::Error> loaded = store.value()->loadColumn(chunk, nullptr);
    REQUIRE(loaded.hasValue());
    CHECK_FALSE(loaded.value());
    // Not saving unedited columns is the whole scheme; a false here is what makes
    // the caller generate instead.
    CHECK(store.value()->stats().columnsLoaded == 0);
}

TEST_CASE("reading a column that was never saved leaves no file behind") {
    // A column is looked up on disk every time one is generated, so a flight across
    // the world would otherwise leave an 8 KiB header for every region it passed
    // over -- in a save whose whole premise is that it grows with what the player
    // did rather than with where they went.
    const TempDir dir;
    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());

    for (i32 region = 0; region < 12; ++region) {
        Chunk chunk(ChunkPos{region * RegionFile::kRegionSize, region});
        REQUIRE_FALSE(store.value()->loadColumn(chunk, nullptr).value());
    }

    usize files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
        if (entry.path().extension() == ".mcr") {
            ++files;
        }
    }
    CHECK(files == 0);
}

TEST_CASE("a saved column comes back through a reopened store") {
    const TempDir dir;
    const ChunkPos pos{-33, 70}; // deliberately in a negative region, index 31

    {
        auto store = WorldStore::open(dir.path(), 1337u);
        REQUIRE(store.hasValue());

        Chunk chunk(pos);
        fillColumn(chunk, blockIdOf("diamond_ore"));
        REQUIRE(store.value()->saveColumn(chunk).hasValue());
        CHECK(store.value()->stats().columnsSaved == 1);
    }

    auto reopened = WorldStore::open(dir.path(), 1337u);
    REQUIRE(reopened.hasValue());

    Chunk restored(pos);
    const Result<bool, WorldStore::Error> loaded =
        reopened.value()->loadColumn(restored, nullptr);
    REQUIRE(loaded.hasValue());
    REQUIRE(loaded.value());

    CHECK(restored.sectionByIndex(0).uniformBlock() == kBedrockBlock);
    CHECK(restored.sectionByIndex(1).uniformBlock() == kStoneBlock);
    CHECK(restored.sectionByIndex(2).get(1, 2, 3) == blockIdOf("diamond_ore"));
    CHECK(restored.sectionByIndex(2).get(31, 31, 31) == blockIdOf("diamond_ore"));
    CHECK(reopened.value()->stats().columnsLoaded == 1);
}

TEST_CASE("negative column coordinates land in the right region") {
    // `-33 >> 5` is -2 and `-33 & 31` is 31. Plain division would put -33 and 33
    // in the same region, which is the bug this arithmetic exists to avoid.
    const TempDir dir;
    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());

    Chunk negative(ChunkPos{-33, -33});
    fillColumn(negative, blockIdOf("gold_ore"));
    REQUIRE(store.value()->saveColumn(negative).hasValue());

    Chunk positive(ChunkPos{33, 33});
    fillColumn(positive, blockIdOf("iron_ore"));
    REQUIRE(store.value()->saveColumn(positive).hasValue());

    Chunk readBack(ChunkPos{-33, -33});
    REQUIRE(store.value()->loadColumn(readBack, nullptr).value());
    CHECK(readBack.sectionByIndex(2).get(1, 2, 3) == blockIdOf("gold_ore"));

    Chunk other(ChunkPos{33, 33});
    REQUIRE(store.value()->loadColumn(other, nullptr).value());
    CHECK(other.sectionByIndex(2).get(1, 2, 3) == blockIdOf("iron_ore"));
}

TEST_CASE("opening a save with the wrong seed is refused") {
    // The check the whole scheme rests on: everything unedited is regenerated from
    // the seed, so edits from another world would land on terrain that was never
    // theirs -- and nothing afterwards could detect it.
    const TempDir dir;
    {
        auto store = WorldStore::open(dir.path(), 1337u);
        REQUIRE(store.hasValue());
    }

    auto wrong = WorldStore::open(dir.path(), 9999u);
    REQUIRE_FALSE(wrong.hasValue());
    CHECK(wrong.error() == WorldStore::Error::SeedMismatch);

    auto right = WorldStore::open(dir.path(), 1337u);
    CHECK(right.hasValue());
}

TEST_CASE("a furnace keeps its contents and its timers across a save") {
    // The defect that made this phase worth doing before mobs: a furnace that
    // forgot what it was smelting when the player walked away.
    const TempDir dir;
    const ChunkPos pos{2, 2};
    const BlockPos furnacePos{70, 64, 70};

    std::vector<SavedFurnace> written;
    {
        Furnace furnace;
        furnace.mutableAt(Furnace::kInputSlot) = ItemStack{itemOfBlock(blockIdOf("iron_ore")), 5};
        furnace.mutableAt(Furnace::kFuelSlot) = ItemStack{itemIdOrNothing("coal"), 2};
        furnace.tick(50); // light it, and get part-way into a smelt
        REQUIRE(furnace.burning());
        written.push_back(captureFurnace(furnacePos, furnace));
    }

    {
        auto store = WorldStore::open(dir.path(), 1337u);
        REQUIRE(store.hasValue());
        Chunk chunk(pos);
        fillColumn(chunk, kStoneBlock);
        REQUIRE(store.value()->saveColumn(chunk, written).hasValue());
    }

    auto reopened = WorldStore::open(dir.path(), 1337u);
    REQUIRE(reopened.hasValue());

    Chunk restored(pos);
    std::vector<SavedFurnace> read;
    REQUIRE(reopened.value()->loadColumn(restored, &read).value());

    REQUIRE(read.size() == 1);
    CHECK(read[0].position == furnacePos);
    CHECK(read[0].slots[Furnace::kInputSlot].item == itemOfBlock(blockIdOf("iron_ore")));
    CHECK(read[0].slots[Furnace::kInputSlot].count == 5);
    CHECK(read[0].timers.burnRemaining == written[0].timers.burnRemaining);
    CHECK(read[0].timers.cookTicks == written[0].timers.cookTicks);

    // And it comes back as a furnace that is still burning, rather than one that
    // relights from its own fuel and loses a smelt.
    Furnace live;
    applyFurnace(read[0], live);
    CHECK(live.burning());
    CHECK(live.cookProgress()
          == doctest::Approx(static_cast<f32>(written[0].timers.cookTicks)
                             / static_cast<f32>(kSmeltTicks)));
}

TEST_CASE("a column with no furnaces reads back an empty list") {
    const TempDir dir;
    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());

    Chunk chunk(ChunkPos{0, 0});
    fillColumn(chunk, kStoneBlock);
    REQUIRE(store.value()->saveColumn(chunk).hasValue());

    Chunk restored(ChunkPos{0, 0});
    std::vector<SavedFurnace> furnaces{SavedFurnace{}}; // must be cleared
    REQUIRE(store.value()->loadColumn(restored, &furnaces).value());
    CHECK(furnaces.empty());
}

TEST_CASE("a corrupt column is reported rather than half-applied") {
    const TempDir dir;
    const ChunkPos pos{0, 0};

    {
        auto store = WorldStore::open(dir.path(), 1337u);
        REQUIRE(store.hasValue());
        Chunk chunk(pos);
        fillColumn(chunk, kStoneBlock);
        REQUIRE(store.value()->saveColumn(chunk).hasValue());
    }

    // Overwrite the column's magic, leaving the region header intact.
    {
        std::fstream file(dir.path() / "r.0.0.mcr",
                          std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(file.is_open());
        // Sector 2, past the 4-byte length and the compression byte.
        file.seekp(2 * static_cast<std::streamoff>(RegionFile::kSectorSize) + 5,
                   std::ios::beg);
        const u32 junk = 0xDEADBEEF;
        file.write(reinterpret_cast<const char*>(&junk), sizeof(junk));
    }

    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());
    Chunk chunk(pos);
    const Result<bool, WorldStore::Error> loaded = store.value()->loadColumn(chunk, nullptr);
    REQUIRE_FALSE(loaded.hasValue());
    CHECK(loaded.error() == WorldStore::Error::Corrupt);
    // The counter is the point: a save that stops working must not look like a
    // save with nothing to do.
    CHECK(store.value()->stats().failures == 1);
}

TEST_CASE("many regions can be touched without running out of open files") {
    // The store keeps a bounded number of region files open and evicts the rest.
    // Eviction must flush, or a column written just before it is lost.
    const TempDir dir;
    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());

    constexpr i32 kRegions = 40; // comfortably past kMaxOpenRegions
    for (i32 region = 0; region < kRegions; ++region) {
        Chunk chunk(ChunkPos{region * RegionFile::kRegionSize, 0});
        fillColumn(chunk, static_cast<BlockId>(kStoneBlock));
        chunk.sectionByIndex(3).set(0, 0, 0, static_cast<BlockId>(region % 8 + 1));
        REQUIRE(store.value()->saveColumn(chunk).hasValue());
    }

    for (i32 region = 0; region < kRegions; ++region) {
        Chunk chunk(ChunkPos{region * RegionFile::kRegionSize, 0});
        const Result<bool, WorldStore::Error> loaded =
            store.value()->loadColumn(chunk, nullptr);
        REQUIRE(loaded.hasValue());
        REQUIRE(loaded.value());
        CHECK(chunk.sectionByIndex(3).get(0, 0, 0) == static_cast<BlockId>(region % 8 + 1));
    }

    CHECK(store.value()->stats().failures == 0);
}

TEST_CASE("concurrent loads from several threads stay consistent") {
    // Loading happens inside the generation job, so up to six workers reach the
    // store at once and two of them can want the same region file.
    const TempDir dir;
    auto store = WorldStore::open(dir.path(), 1337u);
    REQUIRE(store.hasValue());

    constexpr i32 kColumns = 64;
    for (i32 i = 0; i < kColumns; ++i) {
        Chunk chunk(ChunkPos{i, i});
        fillColumn(chunk, static_cast<BlockId>(kStoneBlock));
        chunk.sectionByIndex(3).set(0, 0, 0, static_cast<BlockId>(i % 8 + 1));
        REQUIRE(store.value()->saveColumn(chunk).hasValue());
    }

    std::atomic<i32> wrong{0};
    std::vector<std::thread> workers;
    for (i32 worker = 0; worker < 6; ++worker) {
        workers.emplace_back([&store, &wrong]() {
            for (i32 i = 0; i < kColumns; ++i) {
                Chunk chunk(ChunkPos{i, i});
                const Result<bool, WorldStore::Error> loaded =
                    store.value()->loadColumn(chunk, nullptr);
                if (!loaded.hasValue() || !loaded.value()
                    || chunk.sectionByIndex(3).get(0, 0, 0)
                           != static_cast<BlockId>(i % 8 + 1)) {
                    wrong.fetch_add(1);
                }
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    CHECK(wrong.load() == 0);
    CHECK(store.value()->stats().failures == 0);
}
