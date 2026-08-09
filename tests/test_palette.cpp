#include "world/BlockRegistry.hpp"
#include "world/Palette.hpp"
#include "world/Section.hpp"

#include <doctest/doctest.h>

using namespace mc;

TEST_CASE("a fresh container is uniform and allocates no index array") {
    Palette palette(kSectionVolume, kAirBlock);

    CHECK(palette.isUniform());
    CHECK(palette.bitsPerIndex() == 0);
    CHECK(palette.uniformBlock() == kAirBlock);
    CHECK(palette.get(0) == kAirBlock);
    CHECK(palette.get(kSectionVolume - 1) == kAirBlock);

    // This is the property that makes a 384-block world height affordable:
    // an all-air section costs a palette entry, not 32768 indices.
    CHECK(palette.memoryUsage() < 64);
}

TEST_CASE("writing the uniform value keeps the container uniform") {
    Palette palette(kSectionVolume, kAirBlock);
    palette.set(100, kAirBlock);

    CHECK(palette.isUniform());
    CHECK(palette.bitsPerIndex() == 0);
}

TEST_CASE("the first differing write allocates 1-bit storage") {
    Palette palette(kSectionVolume, kAirBlock);
    palette.set(100, 7);

    CHECK_FALSE(palette.isUniform());
    CHECK(palette.bitsPerIndex() == 1);
    CHECK(palette.paletteSize() == 2);
    CHECK(palette.get(100) == 7);
    CHECK(palette.get(99) == kAirBlock);
    CHECK(palette.get(101) == kAirBlock);
}

TEST_CASE("index width grows as the palette does, preserving all values") {
    Palette palette(1024, kAirBlock);

    // Write 20 distinct block types, forcing 0 -> 1 -> 2 -> 4 -> 8 bits.
    for (u32 i = 0; i < 20; ++i) {
        palette.set(i, static_cast<BlockId>(i + 1));
    }

    CHECK(palette.bitsPerIndex() == 8);
    CHECK(palette.paletteSize() == 21); // 20 written types plus the original air

    for (u32 i = 0; i < 20; ++i) {
        REQUIRE(palette.get(i) == static_cast<BlockId>(i + 1));
    }
    // Everything not written must still read as the original fill.
    for (usize i = 20; i < 1024; ++i) {
        REQUIRE(palette.get(i) == kAirBlock);
    }
}

TEST_CASE("widening preserves values written at the narrower width") {
    Palette palette(256, kAirBlock);

    palette.set(0, 1);              // widens to 1 bit
    palette.set(1, 2);              // widens to 2 bits
    CHECK(palette.bitsPerIndex() == 2);
    CHECK(palette.get(0) == 1);

    palette.set(2, 3);
    palette.set(3, 4);              // widens to 4 bits
    CHECK(palette.bitsPerIndex() == 4);

    CHECK(palette.get(0) == 1);
    CHECK(palette.get(1) == 2);
    CHECK(palette.get(2) == 3);
    CHECK(palette.get(3) == 4);
}

TEST_CASE("fill collapses back to uniform storage") {
    Palette palette(1024, kAirBlock);
    for (u32 i = 0; i < 20; ++i) {
        palette.set(i, static_cast<BlockId>(i + 1));
    }
    REQUIRE(palette.bitsPerIndex() == 8);

    palette.fill(5);

    CHECK(palette.isUniform());
    CHECK(palette.bitsPerIndex() == 0);
    CHECK(palette.uniformBlock() == 5);
    CHECK(palette.get(0) == 5);
    CHECK(palette.get(1023) == 5);
    CHECK(palette.memoryUsage() < 64);
}

TEST_CASE("compact reclaims unused palette entries and narrows the width") {
    Palette palette(1024, kAirBlock);

    for (u32 i = 0; i < 20; ++i) {
        palette.set(i, static_cast<BlockId>(i + 1));
    }
    REQUIRE(palette.bitsPerIndex() == 8);
    REQUIRE(palette.paletteSize() == 21);

    // Overwrite them all with a single type, leaving 19 entries orphaned.
    for (u32 i = 0; i < 20; ++i) {
        palette.set(i, 1);
    }

    // Nothing shrinks on the edit path -- that cost is deliberately deferred.
    CHECK(palette.bitsPerIndex() == 8);

    palette.compact();

    CHECK(palette.paletteSize() == 2);   // air and block 1
    CHECK(palette.bitsPerIndex() == 1);
    for (u32 i = 0; i < 20; ++i) {
        REQUIRE(palette.get(i) == 1);
    }
    REQUIRE(palette.get(20) == kAirBlock);
}

TEST_CASE("compact collapses to uniform when only one type survives") {
    Palette palette(64, kAirBlock);
    palette.set(0, 3);
    for (usize i = 0; i < 64; ++i) {
        palette.set(i, 3);
    }

    palette.compact();

    CHECK(palette.isUniform());
    CHECK(palette.uniformBlock() == 3);
    CHECK(palette.get(63) == 3);
}

TEST_CASE("Section addresses voxels by coordinate") {
    Section section;

    CHECK(section.isEmpty());
    CHECK(section.isUniform());

    section.set(0, 0, 0, kStoneBlock);
    section.set(31, 31, 31, kGrassBlock);

    CHECK_FALSE(section.isEmpty());
    CHECK(section.get(0, 0, 0) == kStoneBlock);
    CHECK(section.get(31, 31, 31) == kGrassBlock);
    CHECK(section.get(15, 15, 15) == kAirBlock);
}

TEST_CASE("Section coordinates and flat indices agree") {
    Section section;
    section.set(5, 9, 13, kSandBlock);

    CHECK(section.getByIndex(localIndex(5, 9, 13)) == kSandBlock);
}
