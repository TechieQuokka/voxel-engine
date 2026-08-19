#include "world/WorldStore.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "world/Chunk.hpp"

#include <cstring>
#include <fstream>
#include <limits>

namespace mc {
namespace {

/// 'MCLV' -- the level file that names a save's world.
constexpr u32 kLevelMagic = 0x564C434Du;
constexpr u16 kLevelVersion = 1;

constexpr const char* kLevelFileName = "level.bin";

/// Header of the level file. Trivially copyable and written as bytes, like
/// everything else here.
struct LevelHeader {
    u32 magic = kLevelMagic;
    u16 version = kLevelVersion;
    u16 reserved = 0;
    u32 seed = 0;
};

/// Arithmetic shift so negative column coordinates floor rather than truncate,
/// for the same reason `blockToSectionCoord` does: plain division would make the
/// region straddling zero twice as wide as every other one.
constexpr i32 kRegionShift = 5; // 32 columns per region
static_assert(1 << kRegionShift == RegionFile::kRegionSize);

} // namespace

const char* describe(WorldStore::Error error) {
    switch (error) {
    case WorldStore::Error::CannotOpen:   return "cannot open the save directory";
    case WorldStore::Error::Io:           return "save read or write failed";
    case WorldStore::Error::Corrupt:      return "save file is not usable";
    case WorldStore::Error::SeedMismatch: return "save belongs to a different world";
    }
    return "unknown error";
}

WorldStore::Address WorldStore::addressOf(ChunkPos pos) {
    return Address{
        ChunkPos{pos.x >> kRegionShift, pos.z >> kRegionShift},
        pos.x & (RegionFile::kRegionSize - 1),
        pos.z & (RegionFile::kRegionSize - 1),
    };
}

std::filesystem::path WorldStore::pathFor(ChunkPos region) const {
    return m_directory / ("r." + std::to_string(region.x) + "." + std::to_string(region.z)
                          + ".mcr");
}

Result<std::unique_ptr<WorldStore>, WorldStore::Error>
WorldStore::open(const std::filesystem::path& directory, u32 seed) {
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    if (code) {
        logError("Cannot create save directory {}: {}", directory.string(), code.message());
        return makeError(Error::CannotOpen);
    }

    const std::filesystem::path levelPath = directory / kLevelFileName;

    if (std::filesystem::exists(levelPath, code) && !code) {
        std::ifstream level(levelPath, std::ios::binary);
        if (!level) {
            return makeError(Error::CannotOpen);
        }
        LevelHeader header;
        level.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!level) {
            return makeError(Error::Io);
        }
        if (header.magic != kLevelMagic || header.version != kLevelVersion) {
            logError("{} is not a level file this build can read", levelPath.string());
            return makeError(Error::Corrupt);
        }
        if (header.seed != seed) {
            // The one check the whole "save only what was edited" scheme rests on.
            // Everything unedited is about to be regenerated from `seed`, and the
            // edits on disk were made in the world `header.seed` produced.
            logError("Save at {} is for seed {}, but this world is seed {}",
                     directory.string(), header.seed, seed);
            return makeError(Error::SeedMismatch);
        }
    } else {
        std::ofstream level(levelPath, std::ios::binary | std::ios::trunc);
        if (!level) {
            return makeError(Error::CannotOpen);
        }
        const LevelHeader header{kLevelMagic, kLevelVersion, 0, seed};
        level.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!level) {
            return makeError(Error::Io);
        }
        logInfo("Created a save at {} for seed {}", directory.string(), seed);
    }

    auto store = std::unique_ptr<WorldStore>(new WorldStore());
    store->m_directory = directory;
    store->m_seed = seed;
    return store;
}

WorldStore::~WorldStore() {
    flush();
}

void WorldStore::evictIfNeeded() {
    if (m_regions.size() <= kMaxOpenRegions) {
        return;
    }

    auto oldest = m_regions.end();
    u64 oldestUse = std::numeric_limits<u64>::max();
    for (auto it = m_regions.begin(); it != m_regions.end(); ++it) {
        if (it->second.lastUsed < oldestUse) {
            oldestUse = it->second.lastUsed;
            oldest = it;
        }
    }
    if (oldest != m_regions.end()) {
        // Closing flushes: the destructor closes the stream, and a stream that has
        // buffered writes it never wrote is the whole failure mode here.
        (void)oldest->second.file->flush();
        m_regions.erase(oldest);
    }
}

Result<RegionFile*, WorldStore::Error> WorldStore::regionFor(ChunkPos region,
                                                             bool createIfMissing) {
    const auto found = m_regions.find(region);
    if (found != m_regions.end()) {
        found->second.lastUsed = ++m_useCounter;
        return found->second.file.get();
    }

    Result<std::unique_ptr<RegionFile>, RegionFile::Error> opened =
        RegionFile::open(pathFor(region), createIfMissing);
    if (!opened) {
        logError("Region {},{}: {}", region.x, region.z, describe(opened.error()));
        return makeError(opened.error() == RegionFile::Error::CannotOpen ? Error::CannotOpen
                         : opened.error() == RegionFile::Error::Corrupt ? Error::Corrupt
                                                                        : Error::Io);
    }
    if (!opened.value()) {
        // Never written. Deliberately not cached: an absent region becomes present
        // the moment a column in it is saved, and a cached "no" would outlive that.
        return static_cast<RegionFile*>(nullptr);
    }

    OpenRegion entry;
    entry.file = std::move(opened).value();
    entry.lastUsed = ++m_useCounter;

    RegionFile* raw = entry.file.get();
    m_regions.emplace(region, std::move(entry));
    evictIfNeeded();
    return raw;
}

Result<bool, WorldStore::Error> WorldStore::loadColumn(Chunk& chunk,
                                                       std::vector<SavedFurnace>* furnaces) {
    const Address address = addressOf(chunk.position());

    std::optional<std::vector<u8>> payload;
    {
        const std::lock_guard<std::mutex> guard(m_mutex);

        Result<RegionFile*, Error> region = regionFor(address.region, false);
        if (!region) {
            ++m_stats.failures;
            return makeError(region.error());
        }
        if (region.value() == nullptr) {
            return false; // No region file, so nothing in it was ever edited.
        }

        Result<std::optional<std::vector<u8>>, RegionFile::Error> read =
            region.value()->read(address.localX, address.localZ);
        if (!read) {
            ++m_stats.failures;
            logError("Column {},{}: {}", chunk.position().x, chunk.position().z,
                     describe(read.error()));
            return makeError(Error::Io);
        }
        payload = std::move(read).value();
    }

    if (!payload.has_value()) {
        return false; // Never edited. The caller generates it.
    }

    // Decoded outside the lock. This is the expensive half -- it recomputes the
    // column's sky light -- and holding the region while six workers queue behind it
    // would serialize the streaming pipeline on one mutex.
    const Result<void, ChunkDecodeError> decoded = decodeChunk(*payload, chunk, furnaces);
    if (!decoded) {
        const std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.failures;
        logError("Column {},{} is on disk but unreadable ({}); regenerating it",
                 chunk.position().x, chunk.position().z, describe(decoded.error()));
        return makeError(Error::Corrupt);
    }

    {
        const std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.columnsLoaded;
    }
    return true;
}

Result<void, WorldStore::Error> WorldStore::saveColumn(const Chunk& chunk,
                                                       std::span<const SavedFurnace> furnaces) {
    // Encoded before the lock, for the same reason decoding happens after it.
    const std::vector<u8> payload = encodeChunk(chunk, furnaces);

    const Address address = addressOf(chunk.position());
    const std::lock_guard<std::mutex> guard(m_mutex);

    Result<RegionFile*, Error> region = regionFor(address.region, true);
    if (!region) {
        ++m_stats.failures;
        return makeError(region.error());
    }
    MC_ASSERT_MSG(region.value() != nullptr, "createIfMissing must produce a region");

    const Result<void, RegionFile::Error> written =
        region.value()->write(address.localX, address.localZ, payload);
    if (!written) {
        ++m_stats.failures;
        logError("Saving column {},{}: {}", chunk.position().x, chunk.position().z,
                 describe(written.error()));
        return makeError(written.error() == RegionFile::Error::TooLarge ? Error::Corrupt
                                                                       : Error::Io);
    }

    ++m_stats.columnsSaved;
    return {};
}

void WorldStore::flush() {
    const std::lock_guard<std::mutex> guard(m_mutex);
    for (auto& [pos, entry] : m_regions) {
        if (!entry.file->flush()) {
            ++m_stats.failures;
            logError("Flushing region {},{} failed", pos.x, pos.z);
        }
    }
}

WorldStore::Stats WorldStore::stats() const {
    const std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

} // namespace mc
