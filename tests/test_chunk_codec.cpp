#include "core/BitPack.hpp"
#include "world/BlockRegistry.hpp"
#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "world/ChunkCodec.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <string_view>

using namespace mc;

namespace {

/// Fills a column with something worth saving: a uniform stone section, an
/// all-air one, and one holding a mix that forces a real index array.
///
/// By reference rather than by value because `Chunk` holds atomics and is
/// therefore neither copyable nor movable.
void makeVariedColumn(Chunk& chunk) {
    chunk.sectionByIndex(0).fill(kBedrockBlock);
    chunk.sectionByIndex(1).fill(kStoneBlock);
    // Section 2 is left as the all-air a fresh column starts with.

    Section& mixed = chunk.sectionByIndex(3);
    mixed.fill(kStoneBlock);
    for (i32 y = 0; y < kSectionSize; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                // Five block types in a pattern that is not a plane, so neither the
                // palette nor the index array can collapse.
                const i32 kind = (x * 7 + y * 3 + z * 5) % 5;
                switch (kind) {
                case 0: mixed.set(x, y, z, kStoneBlock); break;
                case 1: mixed.set(x, y, z, kDirtBlock); break;
                case 2: mixed.set(x, y, z, kSandBlock); break;
                case 3: mixed.set(x, y, z, kAirBlock); break;
                default: mixed.set(x, y, z, kGravelBlock); break;
                }
            }
        }
    }
}

/// True when every voxel of every section matches.
bool sameVoxels(const Chunk& a, const Chunk& b) {
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const Section& left = a.sectionByIndex(index);
        const Section& right = b.sectionByIndex(index);
        for (usize voxel = 0; voxel < kSectionVolume; ++voxel) {
            if (left.getByIndex(voxel) != right.getByIndex(voxel)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("a column survives a round trip voxel for voxel") {
    Chunk original(ChunkPos{3, -7});
    makeVariedColumn(original);

    const std::vector<u8> bytes = encodeChunk(original);
    REQUIRE(!bytes.empty());

    Chunk restored(original.position());
    const Result<void, ChunkDecodeError> decoded = decodeChunk(bytes, restored);
    REQUIRE(decoded.hasValue());

    CHECK(sameVoxels(original, restored));
}

TEST_CASE("a uniform section stays uniform across a round trip") {
    // The whole memory argument for 12 sections per column is that most of them
    // hold no index array. A save that quietly expanded them would still decode
    // correctly and would cost hundreds of megabytes at render distance 16.
    Chunk original(ChunkPos{3, -7});
    makeVariedColumn(original);

    const std::vector<u8> bytes = encodeChunk(original);
    Chunk restored(original.position());
    REQUIRE(decodeChunk(bytes, restored).hasValue());

    CHECK(restored.sectionByIndex(0).isUniform());
    CHECK(restored.sectionByIndex(0).uniformBlock() == kBedrockBlock);
    CHECK(restored.sectionByIndex(2).isEmpty());
    CHECK_FALSE(restored.sectionByIndex(3).isUniform());
}

TEST_CASE("the packed words are written through rather than re-derived") {
    // Persistence is uncompressed on the argument that the words already are the
    // compressed form. That argument only holds if they arrive back unchanged.
    Chunk original(ChunkPos{3, -7});
    makeVariedColumn(original);

    const std::vector<u8> bytes = encodeChunk(original);
    Chunk restored(original.position());
    REQUIRE(decodeChunk(bytes, restored).hasValue());

    const Palette& before = original.sectionByIndex(3).storage();
    const Palette& after = restored.sectionByIndex(3).storage();

    CHECK(after.bitsPerIndex() == before.bitsPerIndex());
    CHECK(after.paletteSize() == before.paletteSize());
    REQUIRE(after.words().size() == before.words().size());
    CHECK(std::memcmp(after.words().data(), before.words().data(),
                      before.words().size() * sizeof(u64)) == 0);
}

TEST_CASE("sky light is rebuilt on load rather than stored") {
    Chunk original(ChunkPos{0, 0});
    // Solid ground with open sky above it: the top sections must come back lit and
    // the buried one dark, without a single light nibble having been written out.
    original.sectionByIndex(0).fill(kStoneBlock);
    original.sectionByIndex(1).fill(kStoneBlock);

    const std::vector<u8> bytes = encodeChunk(original);
    Chunk restored(original.position());
    REQUIRE(decodeChunk(bytes, restored).hasValue());

    CHECK(restored.sectionByIndex(0).skyLight(0, 0, 0) == 0);
    CHECK(restored.sectionByIndex(4).skyLight(0, 0, 0) == 15);
}

TEST_CASE("an empty column encodes to a payload that is small") {
    // Twelve uniform-air sections. Nothing forces this to be small except the
    // absence of index arrays and of light, which is exactly what is being checked.
    const Chunk empty(ChunkPos{0, 0});
    const std::vector<u8> bytes = encodeChunk(empty);

    CHECK(bytes.size() < 256);
}

TEST_CASE("a truncated payload is refused rather than half-applied") {
    Chunk original(ChunkPos{3, -7});
    makeVariedColumn(original);
    std::vector<u8> bytes = encodeChunk(original);
    REQUIRE(bytes.size() > 64);

    bytes.resize(bytes.size() / 2);

    Chunk restored(original.position());
    const Result<void, ChunkDecodeError> decoded = decodeChunk(bytes, restored);
    REQUIRE_FALSE(decoded.hasValue());
    CHECK(decoded.error() == ChunkDecodeError::Truncated);
}

TEST_CASE("a payload that is not a column is refused on its magic") {
    const std::vector<u8> junk(128, 0xAB);

    Chunk restored(ChunkPos{0, 0});
    const Result<void, ChunkDecodeError> decoded = decodeChunk(junk, restored);
    REQUIRE_FALSE(decoded.hasValue());
    CHECK(decoded.error() == ChunkDecodeError::BadMagic);
}

TEST_CASE("an empty payload is truncated, not bad magic") {
    Chunk restored(ChunkPos{0, 0});
    const Result<void, ChunkDecodeError> decoded = decodeChunk({}, restored);
    REQUIRE_FALSE(decoded.hasValue());
    CHECK(decoded.error() == ChunkDecodeError::Truncated);
}

TEST_CASE("a payload from another format version is refused") {
    Chunk original(ChunkPos{3, -7});
    makeVariedColumn(original);
    std::vector<u8> bytes = encodeChunk(original);

    // The version is the u16 straight after the 4-byte magic.
    const u16 wrong = kChunkFormatVersion + 1;
    std::memcpy(bytes.data() + 4, &wrong, sizeof(wrong));

    Chunk restored(original.position());
    const Result<void, ChunkDecodeError> decoded = decodeChunk(bytes, restored);
    REQUIRE_FALSE(decoded.hasValue());
    CHECK(decoded.error() == ChunkDecodeError::BadVersion);
}

TEST_CASE("a block name this build does not know loads as air") {
    // The one case a save from a newer build stays partly readable. Built by hand
    // rather than by encoding, because this build cannot produce the name.
    std::vector<u8> bytes;
    const auto append = [&bytes](const void* data, usize size) {
        const usize offset = bytes.size();
        bytes.resize(offset + size);
        std::memcpy(bytes.data() + offset, data, size);
    };
    const auto put16 = [&append](u16 value) { append(&value, sizeof(value)); };
    const auto put32 = [&append](u32 value) { append(&value, sizeof(value)); };
    const auto put8 = [&append](u8 value) { append(&value, sizeof(value)); };

    put32(0x4C43434Du); // 'MCCL'
    put16(kChunkFormatVersion);
    put16(static_cast<u16>(Chunk::kSectionCount));

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const std::string_view name =
            index == 0 ? std::string_view("unobtainium") : std::string_view("air");
        put16(1); // one palette entry
        put16(static_cast<u16>(name.size()));
        append(name.data(), name.size());
        put8(0);  // uniform, no index array
        put32(0); // no words
    }
    put16(0); // no furnaces

    Chunk restored(ChunkPos{0, 0});
    const Result<void, ChunkDecodeError> decoded = decodeChunk(bytes, restored);
    REQUIRE(decoded.hasValue());
    CHECK(restored.sectionByIndex(0).isUniform());
    CHECK(restored.sectionByIndex(0).uniformBlock() == kAirBlock);
}

TEST_CASE("a palette index pointing past its own palette is refused") {
    // The check that matters: `Palette::get` would otherwise index off the end of
    // the entry vector, on the mesher's innermost loop, from data on disk.
    std::vector<u8> bytes;
    const auto append = [&bytes](const void* data, usize size) {
        const usize offset = bytes.size();
        bytes.resize(offset + size);
        std::memcpy(bytes.data() + offset, data, size);
    };
    const auto put16 = [&append](u16 value) { append(&value, sizeof(value)); };
    const auto put32 = [&append](u32 value) { append(&value, sizeof(value)); };
    const auto put8 = [&append](u8 value) { append(&value, sizeof(value)); };

    put32(0x4C43434Du);
    put16(kChunkFormatVersion);
    put16(static_cast<u16>(Chunk::kSectionCount));

    // Section 0: two palette entries at 1 bit, but every index set to 1... which is
    // in range. Set the width to 1 and the palette to one entry instead, so index 1
    // is addressable by the width and absent from the palette.
    const std::string_view air("air");
    put16(1);
    put16(static_cast<u16>(air.size()));
    append(air.data(), air.size());
    put8(1); // 1 bit per index, addressing entries 0 and 1
    const u32 wordCount = static_cast<u32>(bitpack::wordsNeeded(kSectionVolume, 1));
    put32(wordCount);
    for (u32 word = 0; word < wordCount; ++word) {
        const u64 allOnes = ~u64{0}; // every index is 1; the palette has only entry 0
        append(&allOnes, sizeof(allOnes));
    }

    Chunk restored(ChunkPos{0, 0});
    const Result<void, ChunkDecodeError> decoded = decodeChunk(bytes, restored);
    REQUIRE_FALSE(decoded.hasValue());
    CHECK(decoded.error() == ChunkDecodeError::BadPalette);
}
