#include "world/BlockTable.hpp"
#include "world/BlockUpdates.hpp"
#include "world/FallingBlocks.hpp"
#include "world/Chunk.hpp"
#include "world/Coords.hpp"
#include "world/Section.hpp"
#include "world/World.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace mc;

namespace {

std::unique_ptr<World> readyWorld(i32 renderDistance = 1) {
    auto world = std::make_unique<World>(renderDistance);
    world->updateLoadedRegion(ChunkPos{0, 0});
    world->forEachChunk([](Chunk& chunk) { chunk.setState(ChunkState::Ready); });
    return world;
}

/// A solid floor across a square of the world at `y`, so water has something to run
/// along instead of falling out of an empty test world.
///
/// **Written straight into the sections rather than through `World::setBlock`, and
/// that is a 60x difference rather than a tidiness preference.** Every `setBlock`
/// relights the whole column -- deliberately, see DESIGN.md 7.8 -- which is the right
/// trade on a per-click path and is ruinous on a 625-block loop: the first version of
/// this helper took six seconds per call in an optimised build and minutes in the
/// debug one, and the whole file timed out. This is what a generator does, and it is
/// what a test that wants *terrain* rather than *edits* should do.
void floor(World& world, i32 y, i32 halfExtent = 10) {
    for (i32 x = -halfExtent; x <= halfExtent; ++x) {
        for (i32 z = -halfExtent; z <= halfExtent; ++z) {
            Chunk* chunk = world.find(toChunkPos(BlockPos{x, y, z}));
            REQUIRE(chunk != nullptr);
            Section* section = chunk->sectionAt(blockToSectionCoord(y));
            REQUIRE(section != nullptr);
            section->set(blockToLocalCoord(x), blockToLocalCoord(y),
                         blockToLocalCoord(z), kStoneBlock);
        }
    }
}

/// Runs ticks until the queue empties or `limit` passes. Returns the tick count.
u32 settle(World& world, BlockUpdates& updates, FallingBlocks& falling,
           u32 limit = 4096) {
    u32 ticks = 0;
    while (ticks < limit && updates.pending() > 0) {
        updates.tick(world, falling);
        ++ticks;
    }
    return ticks;
}

u8 levelAt(const World& world, BlockPos pos) {
    const BlockId block = world.blockAt(pos);
    return isFluid(block) ? fluidLevelOf(block) : u8{255};
}

bool isDry(const World& world, BlockPos pos) {
    return !isFluid(world.blockAt(pos));
}

/// A body of water `depth` blocks deep, sitting on a floor, held in by a wall.
///
/// **This is the shape of world every other test in this file leaves out, and the
/// omission hid a bug for the whole life of the feature.** All of them are a single
/// layer of water on stone, so the block under the water is always solid -- and the
/// down-first branch is therefore never asked about water resting on water, which is
/// what every block of every lake and ocean above the bed actually is. Water sat
/// inert in the real game while all eight of those tests passed.
///
/// Written into sections directly for the reason `floor` gives: `setBlock` relights
/// the whole column, and a pool is hundreds of blocks.
void pool(World& world, i32 bedY, i32 depth, i32 wallX, i32 halfExtent = 6) {
    const auto put = [&world](BlockPos pos, BlockId block) {
        Chunk* chunk = world.find(toChunkPos(pos));
        REQUIRE(chunk != nullptr);
        Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
        REQUIRE(section != nullptr);
        section->set(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                     blockToLocalCoord(pos.z), block);
    };

    for (i32 x = -halfExtent; x <= halfExtent; ++x) {
        for (i32 z = -halfExtent; z <= halfExtent; ++z) {
            put(BlockPos{x, bedY, z}, kStoneBlock);
        }
    }
    for (i32 y = bedY + 1; y <= bedY + depth; ++y) {
        for (i32 z = -halfExtent; z <= halfExtent; ++z) {
            put(BlockPos{wallX, y, z}, kStoneBlock);
            for (i32 x = -halfExtent; x < wallX; ++x) {
                put(BlockPos{x, y, z}, kWaterBlock);
            }
        }
    }
}

} // namespace

TEST_CASE("a source with solid ground under it spreads seven blocks and stops") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 40);
    REQUIRE(world->setBlock(BlockPos{0, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 41, 0});
    settle(*world, updates, falling);

    // Level rises by one per block travelled, which is what bounds the spread.
    CHECK(levelAt(*world, BlockPos{0, 41, 0}) == 0);
    for (i32 d = 1; d <= kMaxFluidLevel; ++d) {
        CAPTURE(d);
        CHECK(levelAt(*world, BlockPos{d, 41, 0}) == d);
    }

    // Level 8 would be the eighth block, and vanilla makes that air instead.
    CHECK(isDry(*world, BlockPos{kMaxFluidLevel + 1, 41, 0}));
    CHECK(isDry(*world, BlockPos{-(kMaxFluidLevel + 1), 41, 0}));
}

TEST_CASE("water falls before it spreads, and falling costs no level") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 30);
    REQUIRE(world->setBlock(BlockPos{0, 45, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 45, 0});
    settle(*world, updates, falling);

    // **Down is free.** The whole column from the source to the floor is water, over
    // a drop of fifteen blocks -- far more than the seven a horizontal run reaches.
    for (i32 y = 31; y <= 45; ++y) {
        CAPTURE(y);
        CHECK(isFluid(world->blockAt(BlockPos{0, y, 0})));
    }

    // And nothing spread sideways on the way down: a block that can fall does not
    // also spread horizontally, which is the asymmetry that makes water run downhill
    // rather than diffuse.
    for (i32 y = 32; y <= 45; ++y) {
        CAPTURE(y);
        CHECK(isDry(*world, BlockPos{1, y, 0}));
        CHECK(isDry(*world, BlockPos{0, y, 1}));
    }

    // Having landed, it spreads at full strength -- seven blocks, the same as a
    // source would. That is what "falling water is level 8" means in vanilla.
    CHECK(levelAt(*world, BlockPos{kMaxFluidLevel, 31, 0}) == kMaxFluidLevel);
    CHECK(isDry(*world, BlockPos{kMaxFluidLevel + 1, 31, 0}));
}

TEST_CASE("water finds the hole rather than spreading as a disc") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 40);
    // **A catch basin three blocks down, and it is not decoration.** Without it the
    // hole opens onto 104 blocks of empty test world and the water falls all the way
    // to Y -64, relighting a column per block on the way -- which is what a real
    // world's bedrock floor prevents and what made the first version of this test run
    // for minutes.
    floor(*world, 37);
    // One gap in the upper floor, three blocks east of the source.
    REQUIRE(world->setBlock(BlockPos{3, 40, 0}, kAirBlock) != World::EditStatus::Busy);

    REQUIRE(world->setBlock(BlockPos{0, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 41, 0});
    settle(*world, updates, falling);

    // It ran east, to the hole, and went down it.
    CHECK(isFluid(world->blockAt(BlockPos{1, 41, 0})));
    CHECK(isFluid(world->blockAt(BlockPos{2, 41, 0})));
    CHECK(isFluid(world->blockAt(BlockPos{3, 40, 0})));

    // **And it did not go west at all.** Without the five-block slope search the
    // source would have spread in every direction equally and reached the hole by
    // accident; vanilla looks ahead and commits, which is what makes a flow look like
    // it is obeying gravity.
    CHECK(isDry(*world, BlockPos{-1, 41, 0}));
    CHECK(isDry(*world, BlockPos{0, 41, 1}));
    CHECK(isDry(*world, BlockPos{0, 41, -1}));
}

TEST_CASE("removing the supply drains the flow one block at a time") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 40);
    REQUIRE(world->setBlock(BlockPos{0, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 41, 0});
    settle(*world, updates, falling);
    REQUIRE(isFluid(world->blockAt(BlockPos{4, 41, 0})));

    // Take the source away. Nothing else changes.
    REQUIRE(world->setBlock(BlockPos{0, 41, 0}, kAirBlock) != World::EditStatus::Busy);
    updates.notify(BlockPos{0, 41, 0});
    settle(*world, updates, falling);

    // **Draining needs no second mechanism.** Each block recomputes its level from
    // its neighbours; with no supply the answer is "past level 7", which is air. The
    // cascade falls out of the same queue the spread used.
    for (i32 d = -kMaxFluidLevel; d <= kMaxFluidLevel; ++d) {
        CAPTURE(d);
        CHECK(isDry(*world, BlockPos{d, 41, 0}));
    }
}

TEST_CASE("a source is never consumed by flowing out of it") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 40);
    REQUIRE(world->setBlock(BlockPos{0, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 41, 0});
    settle(*world, updates, falling);

    // **Water is not mass-conserving, and this is the assertion that says so.** The
    // source has fed fourteen or more flowing blocks and is still a source at level
    // 0. A conservative fluid would need per-body global state -- how much water is
    // left, where the surface is now -- which is exactly what a chunk-streaming world
    // cannot cheaply keep. RESEARCH.md 7.1.
    CHECK(world->blockAt(BlockPos{0, 41, 0}) == kWaterBlock);
    CHECK(levelAt(*world, BlockPos{0, 41, 0}) == 0);

    // Dig a hole under it and it keeps producing rather than draining away. The
    // basin below catches the fall; see the note in the slope test.
    floor(*world, 37);
    REQUIRE(world->setBlock(BlockPos{0, 40, 0}, kAirBlock) != World::EditStatus::Busy);
    updates.notify(BlockPos{0, 40, 0});
    settle(*world, updates, falling);

    CHECK(world->blockAt(BlockPos{0, 41, 0}) == kWaterBlock);
    CHECK(isFluid(world->blockAt(BlockPos{0, 40, 0})));
}

TEST_CASE("breaking a wall lets a pool through") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 40);

    // **A wall across, not a single block.** The first version of this test put one
    // stone east of the source and expected a dam; water went around it in two steps,
    // through (1, 41, ±1). That was the engine being right and the test being wrong,
    // and it is worth keeping because it is exactly the mistake a player makes when
    // they try to hold water back with a plug.
    for (i32 z = -10; z <= 10; ++z) {
        Chunk* chunk = world->find(toChunkPos(BlockPos{1, 41, z}));
        REQUIRE(chunk != nullptr);
        Section* section = chunk->sectionAt(blockToSectionCoord(41));
        REQUIRE(section != nullptr);
        section->set(blockToLocalCoord(1), blockToLocalCoord(41),
                     blockToLocalCoord(z), kStoneBlock);
    }
    REQUIRE(world->setBlock(BlockPos{0, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 41, 0});
    settle(*world, updates, falling);

    REQUIRE(world->blockAt(BlockPos{1, 41, 0}) == kStoneBlock);
    CHECK(isDry(*world, BlockPos{2, 41, 0}));

    // Break the wall. This is the player's side of it: an edit notifies, and the
    // water on the other side is what answers.
    REQUIRE(world->setBlock(BlockPos{1, 41, 0}, kAirBlock) != World::EditStatus::Busy);
    updates.notify(BlockPos{1, 41, 0});
    settle(*world, updates, falling);

    CHECK(isFluid(world->blockAt(BlockPos{1, 41, 0})));
    CHECK(isFluid(world->blockAt(BlockPos{2, 41, 0})));
}

TEST_CASE("a flow never overwrites water that is closer to a source") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 40);
    // Two sources six apart, so their flows meet in the middle.
    REQUIRE(world->setBlock(BlockPos{-3, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{3, 41, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{-3, 41, 0});
    updates.notify(BlockPos{3, 41, 0});
    settle(*world, updates, falling);

    // Each block takes the *lowest* level any neighbour can give it, so the answer
    // does not depend on which source was examined first. Order-dependence here would
    // show up as a flow that looks different every time the same world is loaded.
    CHECK(levelAt(*world, BlockPos{-2, 41, 0}) == 1);
    CHECK(levelAt(*world, BlockPos{2, 41, 0}) == 1);
    CHECK(levelAt(*world, BlockPos{0, 41, 0}) == 3);
    CHECK(levelAt(*world, BlockPos{-3, 41, 0}) == 0);
    CHECK(levelAt(*world, BlockPos{3, 41, 0}) == 0);
}

TEST_CASE("water suspends at the edge of the loaded region rather than pouring off it") {
    // A world with exactly one column ready and its neighbours absent. Water at the
    // column edge has to refuse to decide rather than reading the missing neighbour
    // as air.
    auto world = std::make_unique<World>(1);
    world->updateLoadedRegion(ChunkPos{0, 0});
    world->forEachChunk([](Chunk& chunk) {
        chunk.setState(chunk.position() == ChunkPos{0, 0} ? ChunkState::Ready
                                                          : ChunkState::Generating);
    });

    BlockUpdates updates;
    FallingBlocks falling;

    // Floor and source inside the ready column, one block in from its edge.
    const i32 edge = kSectionSize - 1;
    Chunk* ready = world->find(ChunkPos{0, 0});
    REQUIRE(ready != nullptr);
    Section* section = ready->sectionAt(blockToSectionCoord(40));
    REQUIRE(section != nullptr);
    for (i32 x = edge - 3; x <= edge; ++x) {
        for (i32 z = 0; z < 3; ++z) {
            section->set(blockToLocalCoord(x), blockToLocalCoord(40),
                         blockToLocalCoord(z), kStoneBlock);
        }
    }
    REQUIRE(world->setBlock(BlockPos{edge - 3, 41, 0}, kWaterBlock)
            == World::EditStatus::Applied);
    updates.notify(BlockPos{edge - 3, 41, 0});

    // Run a bounded number of ticks. The queue will not empty -- that is the point.
    BlockUpdates::Stats total;
    for (u32 i = 0; i < 64; ++i) {
        const BlockUpdates::Stats stats = updates.tick(*world, falling);
        total.suspended += stats.suspended;
    }

    // **Something suspended**, which is vanilla's own answer: fluid spreads into the
    // first block of a non-ticking chunk and waits there until it loads.
    CHECK(total.suspended > 0);

    // And nothing was written into the column that has not finished generating.
    CHECK(world->blockAt(BlockPos{kSectionSize, 41, 0}) == kAirBlock);

    // The work is not lost -- it is still queued, waiting for the neighbour.
    CHECK(updates.pending() > 0);
}

TEST_CASE("a deep pool flows out of a hole above its bed") {
    // **The case the other eight tests could not reach.** Breaking into a lake
    // anywhere above the bed used to do nothing at all, because the water there rests
    // on water: the down-first branch read "below is not solid" as "I can fall", did
    // nothing because below was already full, and returned before the sideways spread
    // it should have reached. Only the bottom layer of any body of water could move.
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    // Bed at 40, water at 41, 42, 43, wall at x = 1.
    pool(*world, 40, 3, 1);

    SUBCASE("through the top layer") {
        REQUIRE(world->setBlock(BlockPos{1, 43, 0}, kAirBlock) != World::EditStatus::Busy);
        updates.notify(BlockPos{1, 43, 0});
        settle(*world, updates, falling);

        CHECK(isFluid(world->blockAt(BlockPos{1, 43, 0})));
        // The rest of the wall is still standing under it, so the water that got out
        // runs along the top of it rather than falling, one level weaker per block.
        CHECK(levelAt(*world, BlockPos{1, 43, 0}) == 1);
        CHECK(levelAt(*world, BlockPos{2, 43, 0}) == 2);
    }

    SUBCASE("through the middle layer") {
        REQUIRE(world->setBlock(BlockPos{1, 42, 0}, kAirBlock) != World::EditStatus::Busy);
        updates.notify(BlockPos{1, 42, 0});
        settle(*world, updates, falling);

        CHECK(isFluid(world->blockAt(BlockPos{1, 42, 0})));
    }

    SUBCASE("and the pool is not drained by flowing out of itself") {
        REQUIRE(world->setBlock(BlockPos{1, 43, 0}, kAirBlock) != World::EditStatus::Busy);
        updates.notify(BlockPos{1, 43, 0});
        settle(*world, updates, falling);

        // Water is not mass-conserving and this is the whole design: a source is
        // never consumed by flowing out of it. RESEARCH.md 7.1.
        CHECK(levelAt(*world, BlockPos{0, 43, 0}) == 0);
        CHECK(levelAt(*world, BlockPos{-3, 42, 0}) == 0);
    }
}

TEST_CASE("digging the bed out from under a pool drains it downward") {
    // The one case that always worked, kept because it is the other side of the same
    // branch: here `below` really does become air, so the bottom layer falls through.
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    pool(*world, 40, 3, 1);
    // A catch basin, for the reason the slope-search test gives: without one the
    // water falls to the bottom of the world relighting a column per block.
    floor(*world, 36);
    REQUIRE(world->setBlock(BlockPos{0, 40, 0}, kAirBlock) != World::EditStatus::Busy);
    updates.notify(BlockPos{0, 40, 0});
    settle(*world, updates, falling);

    CHECK(isFluid(world->blockAt(BlockPos{0, 40, 0})));
    CHECK(isFluid(world->blockAt(BlockPos{0, 37, 0})));
}

TEST_CASE("a falling column does not spread sideways at any height") {
    // **The guard on the fix, and it is what a first attempt got wrong.** "Water on
    // water spreads sideways" is true of a lake and false of a waterfall, and without
    // the distinction every block of a fall spreads seven blocks in four directions
    // on the way past -- which does not merely look wrong, it stops settling.
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    floor(*world, 30);
    REQUIRE(world->setBlock(BlockPos{0, 45, 0}, kWaterBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{0, 45, 0});
    const u32 ticks = settle(*world, updates, falling);

    // It settled at all, which is the assertion that matters most here.
    CHECK(ticks < 4096);

    // Nothing in the column is a source, and nothing in it spread.
    for (i32 y = 32; y <= 44; ++y) {
        CAPTURE(y);
        CHECK(isFluid(world->blockAt(BlockPos{0, y, 0})));
        CHECK_FALSE(isFluidSource(world->blockAt(BlockPos{0, y, 0})));
        CHECK(isDry(*world, BlockPos{1, y, 0}));
    }
}
