#pragma once

#include "core/Result.hpp"
#include "core/Types.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace mc {

/// A file holding up to 32x32 saved columns, addressed by their position in it.
///
/// **This is Minecraft's region layout, deliberately.** A file per column would put
/// tens of thousands of tiny files in a directory and pay an open() for each; a
/// single file for the world would need its own index anyway. Vanilla's answer is
/// 32x32 columns per file with a sector table at the front, and there is no reason
/// to invent a different one -- the layout is documented, inspectable in a hex
/// editor, and the arithmetic is a shift.
///
/// ```text
///   sector 0     1024 x u32   (offsetInSectors << 8) | sectorCount, 0 when absent
///   sector 1     1024 x u32   Unix seconds when each column was last written
///   sector 2..   the columns, each starting on a sector boundary:
///                  u32  byteLength    of the compression byte plus the payload
///                  u8   compression   kCompressionNone
///                  u8[] payload       a ChunkCodec column
/// ```
///
/// ## Two deliberate departures from vanilla
///
/// **The payload is not compressed, and the compression byte says so.** Vanilla
/// zlib-compresses every chunk. zlib would be this project's seventh dependency, in
/// a build that has kept six and every one of them a foundation (DESIGN.md 4). The
/// sections are palette-compressed already -- typically four bits per voxel -- and
/// only columns the player edited are ever written, so the file grows with what was
/// done rather than with where anyone walked. The byte is in vanilla's position
/// holding vanilla's meaning so that adding zlib later is a new enumerator rather
/// than a format break.
///
/// **The integers are little-endian**, where vanilla's are big. Same reason
/// ChunkCodec's are: one architecture, by constraint.
///
/// ## Concurrency
///
/// **None. One thread at a time, and the caller enforces it.** `WorldStore` holds
/// the lock, because the interesting unit of exclusion is the region rather than
/// this object -- two threads writing neighbouring columns are writing the same
/// file's sector table.
class RegionFile {
public:
    /// Columns along one edge of a region.
    static constexpr i32 kRegionSize = 32;
    static constexpr usize kColumnsPerRegion =
        static_cast<usize>(kRegionSize) * static_cast<usize>(kRegionSize);

    static constexpr usize kSectorSize = 4096;
    /// The offset table and the timestamp table, one sector each.
    static constexpr usize kHeaderSectors = 2;

    /// No compression. The one value this engine writes; see the class comment.
    static constexpr u8 kCompressionNone = 0;

    enum class Error {
        /// The file could not be opened or created at all.
        CannotOpen,
        /// A read or write failed part-way.
        Io,
        /// The header does not describe a usable file.
        Corrupt,
        /// A column too large for the 8-bit sector count in the offset table.
        TooLarge,
    };

    /// Opens `path`, creating an empty region there when `createIfMissing`.
    ///
    /// **Reading must not create**, and that is not a detail. A column is looked up
    /// on disk every time one is generated, so a flight across the world would
    /// otherwise leave an 8 KiB header file for every region it passed over, in a
    /// save whose entire premise is that it grows with what the player did. Loading
    /// passes false and reads a missing file as "nothing saved here"; only writing
    /// creates.
    ///
    /// A null value with no error means the file is not there and was not asked for.
    ///
    /// A `unique_ptr` rather than a value: this owns an `fstream` and a sector map,
    /// and handing it back by value would make every caller think about moves for
    /// no benefit.
    static Result<std::unique_ptr<RegionFile>, Error> open(const std::filesystem::path& path,
                                                           bool createIfMissing);

    RegionFile(const RegionFile&) = delete;
    RegionFile& operator=(const RegionFile&) = delete;

    /// Reads one column's payload, or nullopt when nothing was ever written there.
    ///
    /// `localX` and `localZ` are positions **within** the region, 0 to 31. Turning a
    /// world column position into those is `WorldStore`'s job.
    Result<std::optional<std::vector<u8>>, Error> read(i32 localX, i32 localZ);

    /// Writes one column's payload, replacing whatever was there.
    ///
    /// Rewrites in place when the new payload still fits the sectors already
    /// allocated to it, which is the common case -- a column is saved again and
    /// again as the player keeps digging in it, and its size barely moves.
    ///
    /// **A sector this entry already owns is never freed before the new record is
    /// on disk.** See the comment on `write` in the .cpp: the alternative loses a
    /// whole region rather than a column.
    Result<void, Error> write(i32 localX, i32 localZ, std::span<const u8> payload);

    /// Flushes the stream and `fsync`s the file, so a clean shutdown is durable
    /// rather than merely handed to the page cache. Called when a region is evicted
    /// from `WorldStore`'s cache and when the store is destroyed.
    ///
    /// **Individual writes are deliberately not synced.** A column is saved on every
    /// unload, and an fsync each time would put a disk round trip on the streaming
    /// path. The cost of that choice is bounded: a power loss can leave one column's
    /// header pointing at a record that never landed, and `read` rejects it so the
    /// column regenerates. Losing the region needs the sector map to disagree with
    /// the header, which `write` is careful never to cause.
    Result<void, Error> flush();

    /// How many columns this region currently holds. For tests and the stats line.
    usize storedCount() const;

private:
    RegionFile() = default;

    /// Index into the offset and timestamp tables.
    static usize slotOf(i32 localX, i32 localZ);

    /// Marks the sectors an entry occupies as used. Called while reading the
    /// header at open.
    bool reserve(u32 firstSector, u32 sectorCount);

    /// First run of `sectorCount` free sectors, appending past the end if the file
    /// has no gap large enough. First-fit rather than best-fit: the gaps come from
    /// columns that grew, so they are small and similar, and a scan of a few
    /// hundred bits is not worth a smarter policy.
    u32 allocate(u32 sectorCount);

    /// Grows an entry in place, when the sectors immediately after it are free or
    /// past the end of the file. This is what keeps a column that gained a sector
    /// from moving, and it exists because the obvious way to get the same effect --
    /// releasing the old run and letting `allocate` find it again -- frees sectors
    /// the header still points at. Returns false having changed nothing.
    bool tryExtend(u32 firstSector, u32 haveCount, u32 wantCount);

    void releaseSectors(u32 firstSector, u32 sectorCount);

    Result<void, Error> writeHeaderSlot(usize slot);

    std::filesystem::path m_path;
    std::fstream m_file;
    /// (offsetInSectors << 8) | sectorCount, in vanilla's packing. Zero is absent.
    std::vector<u32> m_offsets;
    std::vector<u32> m_timestamps;
    /// One bit per sector in the file, header sectors included and always set.
    std::vector<bool> m_used;
};

const char* describe(RegionFile::Error error);

} // namespace mc
