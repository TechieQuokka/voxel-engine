#include "world/RegionFile.hpp"

#include "core/Assert.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

// For fsync in `flush`. Linux only, like the rest of the project.
#include <fcntl.h>
#include <unistd.h>

namespace mc {
namespace {

/// Sectors an entry of `bytes` occupies, rounded up.
u32 sectorsFor(usize bytes) {
    return static_cast<u32>((bytes + RegionFile::kSectorSize - 1) / RegionFile::kSectorSize);
}

/// The 8-bit sector count in the offset table is what bounds a column. 255
/// sectors is just under a megabyte, against a worst-case column of about 400 KiB
/// -- twelve sections all non-uniform at the widest index width.
constexpr u32 kMaxSectorsPerColumn = 255;

u32 nowSeconds() {
    using namespace std::chrono;
    return static_cast<u32>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

} // namespace

const char* describe(RegionFile::Error error) {
    switch (error) {
    case RegionFile::Error::CannotOpen: return "cannot open region file";
    case RegionFile::Error::Io:         return "region file read or write failed";
    case RegionFile::Error::Corrupt:    return "region header is not usable";
    case RegionFile::Error::TooLarge:   return "column too large for a region entry";
    }
    return "unknown error";
}

usize RegionFile::slotOf(i32 localX, i32 localZ) {
    MC_ASSERT(localX >= 0 && localX < kRegionSize);
    MC_ASSERT(localZ >= 0 && localZ < kRegionSize);
    return static_cast<usize>(localZ) * static_cast<usize>(kRegionSize)
           + static_cast<usize>(localX);
}

Result<std::unique_ptr<RegionFile>, RegionFile::Error>
RegionFile::open(const std::filesystem::path& path, bool createIfMissing) {
    std::error_code code;
    const bool exists = std::filesystem::exists(path, code) && !code;

    if (!exists && !createIfMissing) {
        // Nothing saved in this region. Not an error, and not a file to make.
        return std::unique_ptr<RegionFile>{};
    }

    auto region = std::unique_ptr<RegionFile>(new RegionFile());

    region->m_offsets.assign(kColumnsPerRegion, 0);
    region->m_timestamps.assign(kColumnsPerRegion, 0);

    // in|out on a file that does not exist fails rather than creating it, so a
    // missing region is created and closed first. `trunc` is deliberate here and
    // only reachable when the file genuinely was not there.
    if (!exists) {
        std::ofstream created(path, std::ios::binary | std::ios::trunc);
        if (!created) {
            return makeError(Error::CannotOpen);
        }
        // An empty header: every slot absent. The file is exactly two sectors.
        const std::vector<u32> blank(kColumnsPerRegion * kHeaderSectors, 0);
        created.write(reinterpret_cast<const char*>(blank.data()),
                      static_cast<std::streamsize>(blank.size() * sizeof(u32)));
        if (!created) {
            return makeError(Error::Io);
        }
    }

    region->m_path = path;
    region->m_file.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!region->m_file) {
        return makeError(Error::CannotOpen);
    }

    region->m_file.seekg(0, std::ios::end);
    const auto fileSize = static_cast<usize>(region->m_file.tellg());
    if (fileSize < kSectorSize * kHeaderSectors) {
        return makeError(Error::Corrupt);
    }

    region->m_file.seekg(0, std::ios::beg);
    region->m_file.read(reinterpret_cast<char*>(region->m_offsets.data()),
                        static_cast<std::streamsize>(kColumnsPerRegion * sizeof(u32)));
    region->m_file.read(reinterpret_cast<char*>(region->m_timestamps.data()),
                        static_cast<std::streamsize>(kColumnsPerRegion * sizeof(u32)));
    if (!region->m_file) {
        return makeError(Error::Io);
    }

    // Rebuild the free-sector map from the header. The map is what stops a file
    // from growing forever as columns are rewritten at changing sizes.
    const u32 totalSectors = sectorsFor(fileSize);
    region->m_used.assign(std::max<usize>(totalSectors, kHeaderSectors), false);
    for (usize sector = 0; sector < kHeaderSectors; ++sector) {
        region->m_used[sector] = true;
    }

    for (usize slot = 0; slot < kColumnsPerRegion; ++slot) {
        const u32 entry = region->m_offsets[slot];
        if (entry == 0) {
            continue;
        }
        const u32 firstSector = entry >> 8;
        const u32 sectorCount = entry & 0xFFu;

        // An entry that overlaps the header, claims no sectors, or runs past the
        // end of the file means the table and the file disagree. Refusing the
        // whole region is right: the alternative is handing out a payload read
        // from somewhere else's bytes.
        if (sectorCount == 0 || firstSector < kHeaderSectors
            || !region->reserve(firstSector, sectorCount)) {
            return makeError(Error::Corrupt);
        }
    }

    return region;
}

bool RegionFile::reserve(u32 firstSector, u32 sectorCount) {
    const usize end = static_cast<usize>(firstSector) + sectorCount;
    if (end > m_used.size()) {
        return false;
    }
    for (usize sector = firstSector; sector < end; ++sector) {
        if (m_used[sector]) {
            return false; // Two entries claiming the same sector.
        }
        m_used[sector] = true;
    }
    return true;
}

void RegionFile::releaseSectors(u32 firstSector, u32 sectorCount) {
    const usize end = std::min<usize>(static_cast<usize>(firstSector) + sectorCount,
                                      m_used.size());
    for (usize sector = firstSector; sector < end; ++sector) {
        m_used[sector] = false;
    }
}

u32 RegionFile::allocate(u32 sectorCount) {
    MC_ASSERT(sectorCount > 0);

    usize run = 0;
    for (usize sector = kHeaderSectors; sector < m_used.size(); ++sector) {
        if (m_used[sector]) {
            run = 0;
            continue;
        }
        ++run;
        if (run == sectorCount) {
            const auto first = static_cast<u32>(sector + 1 - run);
            for (usize i = first; i < first + sectorCount; ++i) {
                m_used[i] = true;
            }
            return first;
        }
    }

    // No gap big enough: extend past the end.
    const auto first = static_cast<u32>(m_used.size());
    m_used.resize(m_used.size() + sectorCount, true);
    return first;
}

bool RegionFile::tryExtend(u32 firstSector, u32 haveCount, u32 wantCount) {
    MC_ASSERT(wantCount > haveCount);

    const usize from = static_cast<usize>(firstSector) + haveCount;
    const usize to = static_cast<usize>(firstSector) + wantCount;
    MC_ASSERT(from <= m_used.size()); // The entry itself is reserved, so it is in the map.

    for (usize sector = from; sector < std::min(to, m_used.size()); ++sector) {
        if (m_used[sector]) {
            return false;
        }
    }

    // Past the end of the file is free by definition; the write extends it.
    if (to > m_used.size()) {
        m_used.resize(to, false);
    }
    for (usize sector = from; sector < to; ++sector) {
        m_used[sector] = true;
    }
    return true;
}

Result<std::optional<std::vector<u8>>, RegionFile::Error>
RegionFile::read(i32 localX, i32 localZ) {
    const u32 entry = m_offsets[slotOf(localX, localZ)];
    if (entry == 0) {
        return std::optional<std::vector<u8>>{};
    }

    const u32 firstSector = entry >> 8;
    const u32 sectorCount = entry & 0xFFu;

    m_file.clear();
    m_file.seekg(static_cast<std::streamoff>(firstSector) * kSectorSize, std::ios::beg);

    u32 byteLength = 0;
    m_file.read(reinterpret_cast<char*>(&byteLength), sizeof(byteLength));
    if (!m_file) {
        return makeError(Error::Io);
    }
    // The length covers the compression byte, so it is at least 1, and it cannot
    // spill past the sectors the header says this entry owns.
    if (byteLength == 0
        || byteLength > static_cast<u32>(sectorCount) * kSectorSize - sizeof(u32)) {
        return makeError(Error::Corrupt);
    }

    u8 compression = 0;
    m_file.read(reinterpret_cast<char*>(&compression), sizeof(compression));
    if (!m_file) {
        return makeError(Error::Io);
    }
    if (compression != kCompressionNone) {
        // Written by a build that had a compressor this one does not. Refusing is
        // right and the column regenerates; the byte exists so this is detectable
        // rather than silently misread as raw bytes.
        return makeError(Error::Corrupt);
    }

    std::vector<u8> payload(byteLength - 1);
    if (!payload.empty()) {
        m_file.read(reinterpret_cast<char*>(payload.data()),
                    static_cast<std::streamsize>(payload.size()));
        if (!m_file) {
            return makeError(Error::Io);
        }
    }

    return std::optional<std::vector<u8>>(std::move(payload));
}

Result<void, RegionFile::Error>
RegionFile::write(i32 localX, i32 localZ, std::span<const u8> payload) {
    const usize slot = slotOf(localX, localZ);

    // The record is the length, the compression byte, and the payload.
    const usize recordBytes = sizeof(u32) + 1 + payload.size();
    const u32 needed = sectorsFor(recordBytes);
    if (needed == 0 || needed > kMaxSectorsPerColumn) {
        return makeError(Error::TooLarge);
    }

    const u32 existing = m_offsets[slot];
    const u32 existingFirst = existing >> 8;
    const u32 existingCount = existing & 0xFFu;

    // **Nothing this entry owns is released until the record is on disk.** Freeing
    // first and reallocating is the obvious way to let a column that grew reuse its
    // own gap, and it is wrong: between the release and a write that then fails, the
    // header still points at those sectors while the map calls them free. The next
    // column to be saved takes them, two header entries end up claiming the same
    // sectors, and `open` answers that by refusing the whole region -- 1024 columns
    // for one failed write. `tryExtend` gets the same reuse without the window.
    u32 firstSector = existingFirst;
    u32 releaseFirst = 0;
    u32 releaseCount = 0;

    if (existing == 0) {
        firstSector = allocate(needed);
    } else if (existingCount < needed) {
        // It outgrew what it had. Grow in place when the sectors after it are free,
        // which is the common case for a column the player keeps digging in.
        if (!tryExtend(existingFirst, existingCount, needed)) {
            firstSector = allocate(needed);
            releaseFirst = existingFirst;
            releaseCount = existingCount;
        }
    } else if (existingCount > needed) {
        // Shrank. Hand back the tail rather than holding sectors that will never
        // be read -- this is the case that would otherwise leak on every save.
        releaseFirst = existingFirst + needed;
        releaseCount = existingCount - needed;
    }

    m_file.clear();
    m_file.seekp(static_cast<std::streamoff>(firstSector) * kSectorSize, std::ios::beg);

    const auto byteLength = static_cast<u32>(1 + payload.size());
    const u8 compression = kCompressionNone;
    m_file.write(reinterpret_cast<const char*>(&byteLength), sizeof(byteLength));
    m_file.write(reinterpret_cast<const char*>(&compression), sizeof(compression));
    if (!payload.empty()) {
        m_file.write(reinterpret_cast<const char*>(payload.data()),
                     static_cast<std::streamsize>(payload.size()));
    }
    if (!m_file) {
        return makeError(Error::Io);
    }

    // Pad out to the sector boundary, so the next allocation past this one starts
    // on real bytes rather than on a hole. A sparse file would read back as zeros
    // and work, but the file size would then not describe the sector map.
    const usize padding = static_cast<usize>(needed) * kSectorSize - recordBytes;
    if (padding > 0) {
        const std::vector<char> zeros(padding, 0);
        m_file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
        if (!m_file) {
            return makeError(Error::Io);
        }
    }

    m_offsets[slot] = (firstSector << 8) | needed;
    m_timestamps[slot] = nowSeconds();

    const Result<void, Error> header = writeHeaderSlot(slot);
    if (!header) {
        // The header did not land, so on disk this slot still names the old record
        // and those sectors have to stay reserved. In memory the table has already
        // moved on to the new ones; the two are reconciled at the next open, which
        // rebuilds the map from the file rather than trusting anything here.
        return header;
    }

    // Safe now: the record is written and the header names it.
    if (releaseCount > 0) {
        releaseSectors(releaseFirst, releaseCount);
    }
    return {};
}

Result<void, RegionFile::Error> RegionFile::writeHeaderSlot(usize slot) {
    // Only the two words that changed. Rewriting all 8 KiB per edit would be a
    // fine cost too, but a column is saved on every unload and this is four bytes.
    m_file.clear();

    m_file.seekp(static_cast<std::streamoff>(slot * sizeof(u32)), std::ios::beg);
    m_file.write(reinterpret_cast<const char*>(&m_offsets[slot]), sizeof(u32));

    const std::streamoff timestampOffset =
        static_cast<std::streamoff>(kSectorSize + slot * sizeof(u32));
    m_file.seekp(timestampOffset, std::ios::beg);
    m_file.write(reinterpret_cast<const char*>(&m_timestamps[slot]), sizeof(u32));

    if (!m_file) {
        return makeError(Error::Io);
    }
    return {};
}

Result<void, RegionFile::Error> RegionFile::flush() {
    m_file.flush();
    if (!m_file) {
        return makeError(Error::Io);
    }

    // `flush` on the stream only hands the bytes to the kernel; a power loss after
    // it still loses them. `fsync` is what makes "quit the game and come back"
    // durable, and this is the right place for it because flush is called on
    // eviction and shutdown rather than per column.
    //
    // A second descriptor is enough: fsync acts on the file, not on the descriptor
    // it was reached through. Opening one here rather than holding it for the
    // object's lifetime keeps `m_file` the only thing that owns the file, at the
    // cost of an open() on a path that runs a handful of times a session.
    const int fd = ::open(m_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return makeError(Error::Io);
    }
    const int synced = ::fsync(fd);
    ::close(fd);
    if (synced != 0) {
        return makeError(Error::Io);
    }
    return {};
}

usize RegionFile::storedCount() const {
    usize count = 0;
    for (const u32 entry : m_offsets) {
        if (entry != 0) {
            ++count;
        }
    }
    return count;
}

} // namespace mc
