#include "world/BlockTable.hpp"
#include "world/BlockUpdates.hpp"
#include "world/FallingBlocks.hpp"
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

/// One tick of updates followed by enough falling-block time to land anything that
/// started this tick from one block up. Returns how many blocks began falling.
usize step(World& world, BlockUpdates& updates, FallingBlocks& falling,
           f32 seconds = 0.05f) {
    const BlockUpdates::Stats stats = updates.tick(world, falling);
    const FallingBlocks::Result result = falling.tick(world, seconds);
    for (const BlockPos& pos : result.landed) {
        updates.notify(pos);
    }
    return stats.fell;
}

/// Runs until nothing is falling and nothing is queued, or `limit` ticks pass.
/// Returns the tick count, so a test can assert the *cascade* rather than just the
/// end state.
u32 settle(World& world, BlockUpdates& updates, FallingBlocks& falling,
           u32 limit = 512) {
    u32 ticks = 0;
    while (ticks < limit && (updates.pending() > 0 || falling.size() > 0)) {
        step(world, updates, falling);
        ++ticks;
    }
    return ticks;
}

} // namespace

TEST_CASE("a block that does not fall is left alone") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    // Stone with nothing under it stays exactly where it is. This is the branch that
    // has to be free, because every edit notifies six neighbours and almost none of
    // them are sand.
    REQUIRE(world->setBlock(BlockPos{4, 40, 4}, kStoneBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{4, 40, 4});

    settle(*world, updates, falling);

    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kStoneBlock);
    CHECK(falling.size() == 0);
}

TEST_CASE("supported sand does not move") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{4, 39, 4}, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{4, 40, 4}, kSandBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{4, 40, 4});

    settle(*world, updates, falling);

    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kSandBlock);
    CHECK(falling.size() == 0);
}

TEST_CASE("sand falls when its support goes -- the phase's exit criterion") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{4, 30, 4}, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{4, 39, 4}, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{4, 40, 4}, kSandBlock) == World::EditStatus::Applied);

    // Dig out the support, exactly as a click would.
    REQUIRE(world->setBlock(BlockPos{4, 39, 4}, kAirBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{4, 39, 4});

    // The first tick should turn the sand into an entity and leave air behind it.
    CHECK(step(*world, updates, falling, 0.0f) == 1);
    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kAirBlock);
    CHECK(falling.size() == 1);

    settle(*world, updates, falling);

    // It comes to rest on the stone at y = 30, which means y = 31.
    CHECK(falling.size() == 0);
    CHECK(world->blockAt(BlockPos{4, 31, 4}) == kSandBlock);
    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kAirBlock);
}

TEST_CASE("gravel falls and stone does not, from the table alone") {
    CHECK(isFalling(kSandBlock));
    CHECK(isFalling(kGravelBlock));
    CHECK_FALSE(isFalling(kStoneBlock));
    CHECK_FALSE(isFalling(kDirtBlock));
    CHECK_FALSE(isFalling(kAirBlock));

    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{8, 20, 8}, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{8, 40, 8}, kGravelBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{8, 40, 8});

    settle(*world, updates, falling);

    CHECK(world->blockAt(BlockPos{8, 21, 8}) == kGravelBlock);
    CHECK(world->blockAt(BlockPos{8, 40, 8}) == kAirBlock);
}

TEST_CASE("a pillar collapses one block per tick, not all at once") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    constexpr i32 kFloor = 20;
    constexpr i32 kBase = 40;
    constexpr i32 kHeight = 6;

    REQUIRE(world->setBlock(BlockPos{2, kFloor, 2}, kStoneBlock)
            == World::EditStatus::Applied);
    // A support under the pillar, so the notification comes from where a click would
    // put it. **Notifying the bottom sand directly is not the same test**: notify
    // queues six face neighbours, so it would hand the tick both the bottom block
    // and the one above it and two would fall at once. What a player does is remove
    // the block *underneath*.
    REQUIRE(world->setBlock(BlockPos{2, kBase - 1, 2}, kStoneBlock)
            == World::EditStatus::Applied);
    for (i32 i = 0; i < kHeight; ++i) {
        REQUIRE(world->setBlock(BlockPos{2, kBase + i, 2}, kSandBlock)
                == World::EditStatus::Applied);
    }

    REQUIRE(world->setBlock(BlockPos{2, kBase - 1, 2}, kAirBlock)
            == World::EditStatus::Applied);
    updates.notify(BlockPos{2, kBase - 1, 2});

    // **One per tick is the behaviour, not an accident of the queue.** Each block
    // that falls notifies the cell it left, and the block above is examined on the
    // *next* tick -- which is what makes a collapse cascade rather than teleport.
    // Ticking with no falling time keeps the entities in the air so only the queue
    // is under test.
    for (i32 i = 0; i < kHeight; ++i) {
        CHECK(step(*world, updates, falling, 0.0f) == 1);
    }
    CHECK(falling.size() == kHeight);

    settle(*world, updates, falling);

    // Six blocks of sand stacked on the floor, and nothing left at the top.
    for (i32 i = 0; i < kHeight; ++i) {
        CHECK(world->blockAt(BlockPos{2, kFloor + 1 + i, 2}) == kSandBlock);
    }
    CHECK(world->blockAt(BlockPos{2, kBase, 2}) == kAirBlock);
    CHECK(falling.size() == 0);
}

TEST_CASE("a pinned column is retried, never dropped") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{4, 20, 4}, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{4, 40, 4}, kSandBlock) == World::EditStatus::Applied);
    updates.notify(BlockPos{4, 40, 4});

    Chunk* column = world->find(ChunkPos{0, 0});
    REQUIRE(column != nullptr);

    // A meshing job owns the column. setBlock refuses, and the update must come back
    // rather than leaving the sand hanging with nothing left to ask about it.
    column->pin();
    for (int i = 0; i < 5; ++i) {
        const BlockUpdates::Stats stats = updates.tick(*world, falling);
        CHECK(stats.retried == 1);
        CHECK(stats.fell == 0);
        CHECK(updates.pending() == 1);
    }
    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kSandBlock);
    column->unpin();

    settle(*world, updates, falling);
    CHECK(world->blockAt(BlockPos{4, 21, 4}) == kSandBlock);
}

TEST_CASE("a landing refused by a pin is held, not lost") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{4, 20, 4}, kStoneBlock) == World::EditStatus::Applied);
    falling.spawn(BlockPos{4, 22, 4}, kSandBlock);

    Chunk* column = world->find(ChunkPos{0, 0});
    REQUIRE(column != nullptr);
    column->pin();

    // Long enough to reach the floor several times over. The entity is the only copy
    // of this block in existence, so a refused landing may not delete it.
    for (int i = 0; i < 20; ++i) {
        const FallingBlocks::Result result = falling.tick(*world, 0.05f);
        CHECK(result.landed.empty());
        CHECK(result.displaced.empty());
    }
    CHECK(falling.size() == 1);
    CHECK(world->blockAt(BlockPos{4, 21, 4}) == kAirBlock);

    column->unpin();
    settle(*world, updates, falling);
    CHECK(world->blockAt(BlockPos{4, 21, 4}) == kSandBlock);
    CHECK(falling.size() == 0);
}

TEST_CASE("an unloaded column discards what was falling through it") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    falling.spawn(BlockPos{4, 100, 4}, kSandBlock);
    CHECK(falling.size() == 1);

    // Stream the region away from it. A flat entity list pays for itself with this
    // sweep, exactly as ItemEntities does.
    world->updateLoadedRegion(ChunkPos{64, 64});
    falling.tick(*world, 0.05f);

    CHECK(falling.size() == 0);
}

TEST_CASE("an unloaded column is not mistaken for unsupported ground") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    // Far outside the loaded region. **`blockAt` answers air for a column that is
    // not loaded**, which is the one reading that could turn "I do not know" into
    // "there is nothing holding this up". It cannot here, because the same answer
    // comes back for the block being examined and air does not fall -- see the note
    // in `examine`, which is also the reason flowing water will need a real test.
    updates.notify(BlockPos{4000, 40, 4000});
    const usize queued = updates.pending();
    CHECK(queued == 7); // the position and its six face neighbours

    const BlockUpdates::Stats stats = updates.tick(*world, falling);
    CHECK(stats.examined == queued);
    CHECK(stats.fell == 0);
    CHECK(stats.retried == 0);
    CHECK(updates.pending() == 0);
    CHECK(falling.size() == 0);
}

TEST_CASE("notify queues a position once, however many times it is told") {
    BlockUpdates updates;

    updates.notify(BlockPos{0, 40, 0});
    CHECK(updates.pending() == 7);

    // The six neighbours of a neighbour overlap heavily; without deduplication a
    // cascade in loose ground would queue the same cells dozens of times.
    updates.notify(BlockPos{0, 40, 0});
    CHECK(updates.pending() == 7);

    updates.notify(BlockPos{0, 41, 0});
    CHECK(updates.pending() == 7 + 5); // (0,42,0) and four sides at y = 41

    updates.clear();
    CHECK(updates.pending() == 0);
}

TEST_CASE("nothing falls out of the bottom of the world") {
    auto world = readyWorld();
    BlockUpdates updates;
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{4, kWorldMinY, 4}, kSandBlock)
            == World::EditStatus::Applied);
    updates.notify(BlockPos{4, kWorldMinY, 4});

    settle(*world, updates, falling);

    CHECK(world->blockAt(BlockPos{4, kWorldMinY, 4}) == kSandBlock);
    CHECK(falling.size() == 0);
}

TEST_CASE("a long delta cannot drop a block through the floor") {
    auto world = readyWorld();
    FallingBlocks falling;

    REQUIRE(world->setBlock(BlockPos{4, 20, 4}, kStoneBlock) == World::EditStatus::Applied);
    falling.spawn(BlockPos{4, 60, 4}, kSandBlock);

    // The frame after a stall, a window drag, or a breakpoint. The landing test asks
    // which cell the bottom is in rather than sweeping to it, so an unclamped step
    // this long would put the block below the floor and it would fall for ever.
    // This is the same failure a 299-second dt found in ItemEntities.
    for (int i = 0; i < 200; ++i) {
        falling.tick(*world, 30.0f);
    }

    CHECK(falling.size() == 0);
    CHECK(world->blockAt(BlockPos{4, 21, 4}) == kSandBlock);
}

TEST_CASE("a block landing on an occupied cell drops instead of being deleted") {
    auto world = readyWorld();
    FallingBlocks falling;

    falling.spawn(BlockPos{4, 30, 4}, kSandBlock);

    // **Landing on top of something is not this case.** A block comes to rest in the
    // cell *above* the first solid one it meets, and that cell is empty by
    // construction -- it is the one the entity is falling through. The only way the
    // resting cell can be occupied is for a block to appear inside the entity while
    // it is in the air, which is what this builds: ground at 29 and a placement into
    // cell 30, where the sand currently is.
    REQUIRE(world->setBlock(BlockPos{4, 29, 4}, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(BlockPos{4, 30, 4}, kStoneBlock) == World::EditStatus::Applied);

    FallingBlocks::Result result;
    for (int i = 0; i < 20 && falling.size() > 0; ++i) {
        result = falling.tick(*world, 0.05f);
    }

    CHECK(falling.size() == 0);
    // The stone that took the cell is untouched, and the sand became an item rather
    // than vanishing.
    CHECK(world->blockAt(BlockPos{4, 30, 4}) == kStoneBlock);
    REQUIRE(result.displaced.size() == 1);
    CHECK(result.displaced[0].block == kSandBlock);
}
