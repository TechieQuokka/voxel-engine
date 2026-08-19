#include "world/ChunkCodec.hpp"

#include "core/Log.hpp"
#include "world/BlockRegistry.hpp"
#include "world/Chunk.hpp"
#include "world/SkyLight.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <string_view>
#include <type_traits>

namespace mc {
namespace {

/// 'MCCL' -- Minecraft-like column. Little-endian, so the bytes on disk read
/// "LCCM" in a hex dump.
constexpr u32 kMagic = 0x4C43434Du;

/// Appends fixed-width fields to a byte vector.
///
/// `memcpy` rather than a reinterpret_cast store: the destination is a `u8*` into
/// a vector and the source is a local, so nothing here is aligned and the cast
/// would be undefined behaviour that happens to work on x86.
class ByteWriter {
public:
    explicit ByteWriter(std::vector<u8>& out) : m_out(out) {}

    template <typename T>
    void put(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const usize offset = m_out.size();
        m_out.resize(offset + sizeof(T));
        std::memcpy(m_out.data() + offset, &value, sizeof(T));
    }

    void putBytes(const void* data, usize size) {
        const usize offset = m_out.size();
        m_out.resize(offset + size);
        std::memcpy(m_out.data() + offset, data, size);
    }

private:
    std::vector<u8>& m_out;
};

/// Reads fixed-width fields out of a byte span, refusing to run off the end.
///
/// Every read is bounds-checked and sets a sticky failure flag rather than
/// throwing, so the decoder can read a whole record and test once at the end
/// instead of testing every field.
class ByteReader {
public:
    explicit ByteReader(std::span<const u8> bytes) : m_bytes(bytes) {}

    template <typename T>
    T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (m_failed || m_offset + sizeof(T) > m_bytes.size()) {
            m_failed = true;
            return value;
        }
        std::memcpy(&value, m_bytes.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return value;
    }

    /// A view into the source bytes, valid as long as the source is. Nothing is
    /// copied: palette names are compared against the registry and never stored.
    std::span<const u8> getBytes(usize size) {
        if (m_failed || m_offset + size > m_bytes.size()) {
            m_failed = true;
            return {};
        }
        const std::span<const u8> view = m_bytes.subspan(m_offset, size);
        m_offset += size;
        return view;
    }

    bool failed() const noexcept { return m_failed; }

private:
    std::span<const u8> m_bytes;
    usize m_offset = 0;
    bool m_failed = false;
};

} // namespace

const char* describe(ChunkDecodeError error) {
    switch (error) {
    case ChunkDecodeError::BadMagic:   return "not a column payload";
    case ChunkDecodeError::BadVersion: return "unknown format version";
    case ChunkDecodeError::Truncated:  return "payload ends mid-field";
    case ChunkDecodeError::Malformed:  return "impossible section header";
    case ChunkDecodeError::BadPalette: return "palette rejected";
    }
    return "unknown error";
}

SavedFurnace captureFurnace(BlockPos position, const Furnace& furnace) {
    SavedFurnace saved;
    saved.position = position;
    saved.timers = furnace.timers();
    for (usize slot = 0; slot < Furnace::kSlots; ++slot) {
        saved.slots[slot] = furnace.at(slot);
    }
    return saved;
}

void applyFurnace(const SavedFurnace& saved, Furnace& furnace) {
    furnace.restoreTimers(saved.timers);
    for (usize slot = 0; slot < Furnace::kSlots; ++slot) {
        // `mutableAt` is documented as never being called for an output slot,
        // because reaching one through the click path would hand out a free result.
        // Restoring is not that path: the output was already smelted, and refusing
        // to write it back would lose whatever the furnace had finished.
        furnace.mutableAt(slot) = saved.slots[slot];
    }
}

std::vector<u8> encodeChunk(const Chunk& chunk, std::span<const SavedFurnace> furnaces) {
    const BlockRegistry& registry = BlockRegistry::instance();

    std::vector<u8> out;
    ByteWriter writer(out);

    writer.put(kMagic);
    writer.put(kChunkFormatVersion);
    writer.put(static_cast<u16>(Chunk::kSectionCount));

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const Palette& storage = chunk.sectionByIndex(index).storage();
        const std::span<const BlockId> entries = storage.entries();

        writer.put(static_cast<u16>(entries.size()));
        for (const BlockId id : entries) {
            const std::string_view name = registry[id].name;
            writer.put(static_cast<u16>(name.size()));
            writer.putBytes(name.data(), name.size());
        }

        // The words go out exactly as they sit in memory. This is the step that
        // makes an uncompressed save defensible: they are already four bits per
        // voxel for typical terrain, and a uniform section contributes none.
        const std::span<const u64> words = storage.words();
        writer.put(static_cast<u8>(storage.bitsPerIndex()));
        writer.put(static_cast<u32>(words.size()));
        writer.putBytes(words.data(), words.size() * sizeof(u64));
    }

    writer.put(static_cast<u16>(furnaces.size()));
    for (const SavedFurnace& saved : furnaces) {
        writer.put(saved.position.x);
        writer.put(saved.position.y);
        writer.put(saved.position.z);

        writer.put(saved.timers.burnRemaining);
        writer.put(saved.timers.burnTotal);
        writer.put(saved.timers.cookTicks);

        for (const ItemStack& stack : saved.slots) {
            // An empty slot writes an empty name rather than an id, so that "no
            // item" and "an item this build lost" stay distinguishable on load.
            const std::string_view name =
                stack.empty() ? std::string_view{} : itemName(stack.item);
            writer.put(static_cast<u16>(name.size()));
            writer.putBytes(name.data(), name.size());
            writer.put(stack.empty() ? 0u : stack.count);
        }
    }

    return out;
}

Result<void, ChunkDecodeError> decodeChunk(std::span<const u8> bytes, Chunk& chunk,
                                           std::vector<SavedFurnace>* furnaces) {
    const BlockRegistry& registry = BlockRegistry::instance();

    ByteReader reader(bytes);

    if (reader.get<u32>() != kMagic) {
        return makeError(reader.failed() ? ChunkDecodeError::Truncated
                                         : ChunkDecodeError::BadMagic);
    }
    if (reader.get<u16>() != kChunkFormatVersion) {
        return makeError(reader.failed() ? ChunkDecodeError::Truncated
                                         : ChunkDecodeError::BadVersion);
    }
    if (reader.get<u16>() != static_cast<u16>(Chunk::kSectionCount)) {
        return makeError(reader.failed() ? ChunkDecodeError::Truncated
                                         : ChunkDecodeError::Malformed);
    }

    // Palette entries are resolved into this and handed to `fromParts` as a span.
    // Reused across sections so a column costs one allocation rather than twelve.
    std::vector<BlockId> entries;

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const auto paletteSize = reader.get<u16>();
        if (reader.failed()) {
            return makeError(ChunkDecodeError::Truncated);
        }
        // 256 is what 8-bit indices address, and 8 is the widest supported width.
        if (paletteSize == 0 || paletteSize > 256) {
            return makeError(ChunkDecodeError::Malformed);
        }

        entries.clear();
        entries.reserve(paletteSize);
        for (u16 entry = 0; entry < paletteSize; ++entry) {
            const auto nameLength = reader.get<u16>();
            const std::span<const u8> name = reader.getBytes(nameLength);
            if (reader.failed()) {
                return makeError(ChunkDecodeError::Truncated);
            }

            const std::string_view text(reinterpret_cast<const char*>(name.data()),
                                        name.size());
            const std::optional<BlockId> id = registry.findByName(text);
            if (id.has_value()) {
                entries.push_back(*id);
                continue;
            }

            // A block this build does not have. Air keeps the column loadable and
            // keeps every *other* block in it correct, which is worth more than
            // refusing the file -- and it is visible, so it does not pass unnoticed.
            logWarn("Save names block '{}', which this build has no id for; loaded as air",
                    text);
            entries.push_back(kAirBlock);
        }

        const auto bits = reader.get<u8>();
        const auto wordCount = reader.get<u32>();
        if (reader.failed()) {
            return makeError(ChunkDecodeError::Truncated);
        }

        const std::span<const u8> raw = reader.getBytes(usize{wordCount} * sizeof(u64));
        if (reader.failed()) {
            return makeError(ChunkDecodeError::Truncated);
        }

        // Copied rather than viewed in place: the payload is a byte span with no
        // alignment guarantee, and a u64 span over it would be misaligned.
        std::vector<u64> words(wordCount);
        std::memcpy(words.data(), raw.data(), raw.size());

        std::optional<Palette> storage =
            Palette::fromParts(kSectionVolume, entries, bits, words);
        if (!storage.has_value()) {
            return makeError(ChunkDecodeError::BadPalette);
        }
        chunk.sectionByIndex(index).storage() = std::move(*storage);
    }

    if (furnaces != nullptr) {
        furnaces->clear();
    }

    const auto furnaceCount = reader.get<u16>();
    if (reader.failed()) {
        return makeError(ChunkDecodeError::Truncated);
    }
    for (u16 entry = 0; entry < furnaceCount; ++entry) {
        SavedFurnace saved;
        saved.position.x = reader.get<i32>();
        saved.position.y = reader.get<i32>();
        saved.position.z = reader.get<i32>();

        saved.timers.burnRemaining = reader.get<u32>();
        saved.timers.burnTotal = reader.get<u32>();
        saved.timers.cookTicks = reader.get<u32>();
        if (reader.failed()) {
            return makeError(ChunkDecodeError::Truncated);
        }

        for (usize slot = 0; slot < Furnace::kSlots; ++slot) {
            const auto nameLength = reader.get<u16>();
            const std::span<const u8> name = reader.getBytes(nameLength);
            const auto count = reader.get<u32>();
            if (reader.failed()) {
                return makeError(ChunkDecodeError::Truncated);
            }

            const std::string_view text(reinterpret_cast<const char*>(name.data()),
                                        name.size());
            const ItemId item = itemIdOrNothing(text);
            // `itemIdOrNothing` answers kNoItem for both an empty name and a name
            // this build lost, and only the second is worth a word.
            if (item == kNoItem && !text.empty()) {
                logWarn("Save names item '{}' in a furnace, which this build has no id "
                        "for; the slot is empty",
                        text);
            }

            ItemStack& stack = saved.slots[slot];
            if (item == kNoItem || count == 0) {
                stack.clear();
                continue;
            }
            stack.item = item;
            // Clamped, because a count past the stack limit would let a saved file
            // hand the player more than a slot can hold.
            stack.count = std::min(count, maxStackOf(item));
        }

        if (furnaces != nullptr) {
            furnaces->push_back(saved);
        }
    }

    // Derived, so it is rebuilt rather than stored. See the header.
    computeSkyLight(chunk);

    return {};
}

} // namespace mc
