#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "world/LightArray.hpp"
#include "world/SkyLight.hpp"

#include <doctest/doctest.h>

#include <vector>

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

TEST_CASE("sky light is a function of opacity and nothing else") {
    // **This is the precondition `World::setBlock` skips the relight on.** It asks
    // `isOpaque(previous) == isOpaque(block)` and returns without recomputing,
    // which is only sound while opacity is the whole of this pass's input.
    //
    // The case that would break it is vanilla's: water attenuating daylight a level
    // per block. Adding that here without giving `setBlock` a matching condition
    // would leave flowing water lit by whatever the column held before it arrived,
    // and nothing else in the suite would notice -- which is why the invariant is
    // asserted rather than left as a comment on the guard.
    Chunk chunk({0, 0});
    constexpr i32 kTop = 40;
    fillGround(chunk, kTop);

    // A pool sunk into the surface, so water sits where daylight actually reaches
    // it. Sealed rock would prove nothing: it is uniform 0 either way.
    for (i32 z = 4; z < 12; ++z) {
        for (i32 x = 4; x < 12; ++x) {
            setBlock(chunk, x, kTop, z, kAirBlock);
        }
    }
    computeSkyLight(chunk);

    std::vector<u8> before;
    before.reserve(static_cast<usize>(kSectionSize * kSectionSize));
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            before.push_back(lightAt(chunk, x, kTop, z));
        }
    }

    // Fill the pool. Water is non-opaque and so is the air it replaces, so every
    // level above has to come back identical.
    REQUIRE(isOpaque(kWaterBlock) == isOpaque(kAirBlock));
    for (i32 z = 4; z < 12; ++z) {
        for (i32 x = 4; x < 12; ++x) {
            setBlock(chunk, x, kTop, z, kWaterBlock);
        }
    }
    computeSkyLight(chunk);

    usize index = 0;
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            CHECK(lightAt(chunk, x, kTop, z) == before[index++]);
        }
    }

    // And the other direction, so the test cannot pass by the pass having stopped
    // working altogether: an opacity change *must* move something.
    for (i32 z = 4; z < 12; ++z) {
        for (i32 x = 4; x < 12; ++x) {
            setBlock(chunk, x, kTop, z, kStoneBlock);
        }
    }
    CHECK(computeSkyLight(chunk) != 0);
}

TEST_CASE("every fluid is non-opaque, so a flow never owes a sky-light relight") {
    // The guard in `World::setBlock` turns a fluid tick from ~1 ms per edited cell
    // into nothing, and it does that only because air and every water block agree
    // on `opaque`. A fluid added later that did not -- lava is the one on the list
    // -- would silently reintroduce the cost this removed.
    CHECK_FALSE(isOpaque(kAirBlock));
    for (usize i = 0; i < kBlocks.size(); ++i) {
        const auto id = static_cast<BlockId>(i);
        if (!isFluid(id)) {
            continue;
        }
        CAPTURE(kBlocks[i].name);
        CHECK_FALSE(isOpaque(id));
    }
}
