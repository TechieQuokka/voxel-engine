#include "world/BlockRegistry.hpp"
#include "world/Chunk.hpp"

#include <doctest/doctest.h>

using namespace mc;

TEST_CASE("a column covers exactly the world's vertical range") {
    CHECK(Chunk::kSectionCount == 12);
    CHECK(kMinSectionY == -2);
    CHECK(kMaxSectionY == 10);
    CHECK(sectionIndexToWorldY(0) == kWorldMinY);
    CHECK(sectionIndexToWorldY(static_cast<i32>(Chunk::kSectionCount) - 1)
          == kWorldMaxY - kSectionSize);
}

TEST_CASE("sectionAt maps world section Y to the right section") {
    Chunk chunk(ChunkPos{3, -7});
    CHECK(chunk.position() == ChunkPos{3, -7});

    // Tag each section through the by-index accessor, then read it back by
    // section Y. A mistake in the index arithmetic shows up as a shifted tag.
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        chunk.sectionByIndex(index).set(0, 0, 0, static_cast<BlockId>(index % 5));
    }

    for (i32 sectionY = kMinSectionY; sectionY < kMaxSectionY; ++sectionY) {
        const Section* section = chunk.sectionAt(sectionY);
        REQUIRE(section != nullptr);
        const auto expected = static_cast<BlockId>(
            static_cast<usize>(sectionIndexInColumn(sectionY)) % 5);
        CHECK(section->get(0, 0, 0) == expected);
    }
}

TEST_CASE("sectionAt returns null outside the world, rather than asserting") {
    // Neighbour lookups legitimately ask about the section above the sky and the
    // one below bedrock, so this has to be an answer and not a crash.
    Chunk chunk(ChunkPos{0, 0});

    CHECK(chunk.sectionAt(kMinSectionY - 1) == nullptr);
    CHECK(chunk.sectionAt(kMaxSectionY) == nullptr);
    CHECK(chunk.sectionAt(kMinSectionY) != nullptr);
    CHECK(chunk.sectionAt(kMaxSectionY - 1) != nullptr);
}

TEST_CASE("a fresh column is all air and needs no work") {
    Chunk chunk(ChunkPos{0, 0});

    CHECK(chunk.state() == ChunkState::Empty);
    CHECK_FALSE(chunk.anyDirty());
    CHECK(chunk.dirtyMask() == 0);

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        CHECK(chunk.sectionByIndex(index).isEmpty());
    }
}

TEST_CASE("the dirty mask tracks sections independently") {
    Chunk chunk(ChunkPos{0, 0});

    chunk.markSectionDirty(0);
    chunk.markSectionDirty(11);

    CHECK(chunk.anyDirty());
    CHECK(chunk.isSectionDirty(0));
    CHECK(chunk.isSectionDirty(11));
    CHECK_FALSE(chunk.isSectionDirty(5));

    chunk.clearSectionDirty(0);
    CHECK_FALSE(chunk.isSectionDirty(0));
    CHECK(chunk.isSectionDirty(11));
    CHECK(chunk.anyDirty());

    chunk.clearSectionDirty(11);
    CHECK_FALSE(chunk.anyDirty());
}

TEST_CASE("markAllDirty sets exactly the twelve section bits") {
    Chunk chunk(ChunkPos{0, 0});
    chunk.markAllDirty();

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        CHECK(chunk.isSectionDirty(index));
    }
    // No bits above the section count, or a scheduler loop would find phantom work.
    CHECK(chunk.dirtyMask() == 0x0FFF);
}

TEST_CASE("state survives a round trip") {
    Chunk chunk(ChunkPos{0, 0});

    chunk.setState(ChunkState::Generating);
    CHECK(chunk.state() == ChunkState::Generating);
    chunk.setState(ChunkState::Ready);
    CHECK(chunk.state() == ChunkState::Ready);
}

TEST_CASE("memory usage grows only when a section stops being uniform") {
    Chunk chunk(ChunkPos{0, 0});
    const usize uniform = chunk.memoryUsage();

    // A uniform column is not free -- each Palette holds a one-entry vector -- but
    // it carries no index array, which is the property that makes 12 sections per
    // column affordable.
    chunk.sectionByIndex(4).set(1, 2, 3, kStoneBlock);
    const usize mixed = chunk.memoryUsage();

    CHECK(mixed > uniform);
    CHECK(chunk.sectionByIndex(4).get(1, 2, 3) == kStoneBlock);
    CHECK_FALSE(chunk.sectionByIndex(4).isUniform());
    CHECK(chunk.sectionByIndex(3).isUniform());
}
