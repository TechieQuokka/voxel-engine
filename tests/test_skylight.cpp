#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "world/LightArray.hpp"
#include "world/SkyLight.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

/// Fills a column solid up to and including `top`, leaving air above.
void fillGround(Chunk& chunk, i32 top) {
    for (i32 y = kWorldMinY; y <= top; ++y) {
        Section* section = chunk.sectionAt(blockToSectionCoord(y));
        REQUIRE(section != nullptr);
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                section->set(x, blockToLocalCoord(y), z, kStoneBlock);
            }
        }
    }
}

u8 lightAt(const Chunk& chunk, i32 x, i32 y, i32 z) {
    const Section* section = chunk.sectionAt(blockToSectionCoord(y));
    REQUIRE(section != nullptr);
    return section->skyLight(x, blockToLocalCoord(y), z);
}

void setBlock(Chunk& chunk, i32 x, i32 y, i32 z, BlockId block) {
    Section* section = chunk.sectionAt(blockToSectionCoord(y));
    REQUIRE(section != nullptr);
    section->set(x, blockToLocalCoord(y), z, block);
}

} // namespace

TEST_CASE("LightArray stays uniform until something disagrees") {
    LightArray light(15);

    CHECK(light.isUniform());
    CHECK(light.memoryUsage() == 0);
    CHECK(light.get(0) == 15);
    CHECK(light.get(kSectionVolume - 1) == 15);

    // Writing the value it already holds must not allocate: most of the world does
    // exactly this, and if it expanded, the collapse would buy nothing.
    light.set(100, 15);
    CHECK(light.isUniform());
    CHECK(light.memoryUsage() == 0);

    light.set(100, 4);
    CHECK_FALSE(light.isUniform());
    CHECK(light.get(100) == 4);
    CHECK(light.get(101) == 15);
}

TEST_CASE("LightArray packs two voxels per byte without disturbing its neighbour") {
    LightArray light;

    // Both halves of one byte, which is where a nibble shift goes wrong.
    light.set(10, 3);
    light.set(11, 12);
    CHECK(light.get(10) == 3);
    CHECK(light.get(11) == 12);

    light.set(10, 9);
    CHECK(light.get(10) == 9);
    CHECK(light.get(11) == 12);
}

TEST_CASE("LightArray::fill collapses back to uniform") {
    LightArray light;
    light.set(5, 7);
    REQUIRE_FALSE(light.isUniform());

    light.fill(0);
    CHECK(light.isUniform());
    CHECK(light.memoryUsage() == 0);
    CHECK(light.get(5) == 0);
}

TEST_CASE("open sky is fully lit and the ground below it is dark") {
    Chunk chunk({0, 0});
    constexpr i32 kTop = 40;
    fillGround(chunk, kTop);

    computeSkyLight(chunk);

    CHECK(lightAt(chunk, 4, kTop + 1, 4) == 15);
    CHECK(lightAt(chunk, 4, kTop + 20, 4) == 15);

    // Sealed rock never sees daylight, however bright it is directly above.
    CHECK(lightAt(chunk, 4, kTop - 5, 4) == 0);
}

TEST_CASE("a shaft carries full daylight to its floor without dimming") {
    // The rule that separates sky light from a generic flood fill: straight down
    // through transparent blocks costs nothing, so a hole in a cave roof is a
    // column of full daylight rather than a gradient.
    Chunk chunk({0, 0});
    constexpr i32 kTop = 60;
    fillGround(chunk, kTop);

    constexpr i32 kShaftX = 16;
    constexpr i32 kShaftZ = 16;
    for (i32 y = kTop; y > kTop - 30; --y) {
        setBlock(chunk, kShaftX, y, kShaftZ, kAirBlock);
    }

    computeSkyLight(chunk);

    CHECK(lightAt(chunk, kShaftX, kTop, kShaftZ) == 15);
    CHECK(lightAt(chunk, kShaftX, kTop - 29, kShaftZ) == 15);
}

TEST_CASE("light entering a side tunnel falls off one level per block") {
    Chunk chunk({0, 0});
    constexpr i32 kTop = 60;
    fillGround(chunk, kTop);

    // A shaft down to a horizontal tunnel running away from it.
    constexpr i32 kShaftX = 4;
    constexpr i32 kZ = 16;
    constexpr i32 kFloor = kTop - 10;
    for (i32 y = kTop; y >= kFloor; --y) {
        setBlock(chunk, kShaftX, y, kZ, kAirBlock);
    }
    for (i32 x = kShaftX; x < kShaftX + 20; ++x) {
        setBlock(chunk, x, kFloor, kZ, kAirBlock);
    }

    computeSkyLight(chunk);

    REQUIRE(lightAt(chunk, kShaftX, kFloor, kZ) == 15);
    CHECK(lightAt(chunk, kShaftX + 1, kFloor, kZ) == 14);
    CHECK(lightAt(chunk, kShaftX + 2, kFloor, kZ) == 13);
    CHECK(lightAt(chunk, kShaftX + 5, kFloor, kZ) == 10);

    // Fifteen blocks in, daylight has run out entirely.
    CHECK(lightAt(chunk, kShaftX + 15, kFloor, kZ) == 0);
    CHECK(lightAt(chunk, kShaftX + 19, kFloor, kZ) == 0);
}

TEST_CASE("only the section holding the surface stores a light array") {
    // The feasibility argument, asserted. Open air above the terrain is uniform 15
    // and sealed rock is uniform 0, so neither allocates; over flat ground exactly
    // one section -- the one daylight stops inside -- has anything to store.
    //
    // If this regresses to every section, render distance 16 gains hundreds of MiB
    // for a channel that is constant almost everywhere.
    Chunk chunk({0, 0});
    constexpr i32 kTop = 40;
    fillGround(chunk, kTop);
    computeSkyLight(chunk);

    const auto surfaceSection =
        static_cast<usize>(sectionIndexInColumn(blockToSectionCoord(kTop)));

    usize allocating = 0;
    for (usize i = 0; i < Chunk::kSectionCount; ++i) {
        const bool stores = chunk.sectionByIndex(i).skyLightArray().memoryUsage() != 0;
        if (stores) {
            ++allocating;
            CHECK(i == surfaceSection);
        }
    }
    CHECK(allocating == 1);
}
