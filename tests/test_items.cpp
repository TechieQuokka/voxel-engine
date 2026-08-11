#include "world/BlockTable.hpp"
#include "world/Inventory.hpp"
#include "world/ItemEntities.hpp"
#include "world/World.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace mc;

namespace {

/// A world with a solid floor at y = 63 across the loaded columns, so dropped items
/// have something to land on.
std::unique_ptr<World> floorWorld(i32 renderDistance = 1) {
    auto world = std::make_unique<World>(renderDistance);
    world->updateLoadedRegion(ChunkPos{0, 0});
    world->forEachChunk([](Chunk& chunk) {
        Section* section = chunk.sectionAt(blockToSectionCoord(63));
        REQUIRE(section != nullptr);
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                section->set(x, blockToLocalCoord(63), z, kStoneBlock);
            }
        }
        chunk.setState(ChunkState::Ready);
    });
    return world;
}

/// Runs `seconds` of simulation at a fixed 60 Hz step.
void simulate(ItemEntities& items, const World& world, f32 seconds) {
    constexpr f32 kStep = 1.0f / 60.0f;
    for (f32 elapsed = 0.0f; elapsed < seconds; elapsed += kStep) {
        items.tick(world, kStep);
    }
}

} // namespace

TEST_CASE("dropped items fall and land on the ground") {
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 70.0f, 8.5f}, kStoneBlock, 1);
    REQUIRE(items.size() == 1);

    simulate(items, *world, 3.0f);

    REQUIRE(items.size() == 1);
    const ItemEntity& item = items.items().front();
    CHECK(item.onGround);
    // The floor's top face is at y = 64, and the item rests just above it.
    CHECK(item.position.y > 64.0f);
    CHECK(item.position.y < 64.4f);
}

TEST_CASE("stacks of the same block merge, different blocks do not") {
    auto world = floorWorld();
    ItemEntities items;

    for (u32 i = 0; i < 5; ++i) {
        items.spawn(vec3{8.5f, 66.0f, 8.5f}, kStoneBlock, 1);
    }
    items.spawn(vec3{8.5f, 66.0f, 8.5f}, kDirtBlock, 1);
    REQUIRE(items.size() == 6);

    simulate(items, *world, 4.0f);

    // The five stones become one stack of five; the dirt stays its own entity.
    u32 stoneTotal = 0;
    u32 dirtEntities = 0;
    for (const ItemEntity& item : items.items()) {
        if (item.block == kStoneBlock) {
            stoneTotal += item.count;
        } else if (item.block == kDirtBlock) {
            ++dirtEntities;
        }
    }
    CHECK(stoneTotal == 5);
    CHECK(dirtEntities == 1);
    CHECK(items.size() < 6);
}

TEST_CASE("an item cannot be picked up before its delay expires") {
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 66.0f, 8.5f}, kStoneBlock, 1);

    // Straight away: refused, so a broken block does not fly back into the hand
    // that broke it.
    CHECK(items.collect(vec3{8.5f, 66.0f, 8.5f}, 2.0f).empty());
    CHECK(items.size() == 1);

    simulate(items, *world, 1.0f);

    const auto picked = items.collect(vec3{8.5f, 65.0f, 8.5f}, 3.0f);
    REQUIRE(picked.size() == 1);
    CHECK(picked[0].block == kStoneBlock);
    CHECK(picked[0].count == 1);
    CHECK(items.size() == 0);
}

TEST_CASE("collect only takes what is in range") {
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 66.0f, 8.5f}, kStoneBlock, 1);
    items.spawn(vec3{25.5f, 66.0f, 25.5f}, kDirtBlock, 1);
    simulate(items, *world, 1.0f);

    const auto picked = items.collect(vec3{8.5f, 65.0f, 8.5f}, 2.0f);
    REQUIRE(picked.size() == 1);
    CHECK(picked[0].block == kStoneBlock);
    CHECK(items.size() == 1); // The far one is untouched.
}

TEST_CASE("items despawn, and do not linger for a whole session") {
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 66.0f, 8.5f}, kStoneBlock, 1);

    // Two ticks either side of the timer rather than five real minutes of stepping.
    items.tick(*world, ItemEntities::kDespawnSeconds - 1.0f);
    CHECK(items.size() == 1);
    items.tick(*world, 2.0f);
    CHECK(items.size() == 0);
}

TEST_CASE("an item whose column unloads goes with it") {
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 66.0f, 8.5f}, kStoneBlock, 1);
    simulate(items, *world, 0.5f);
    REQUIRE(items.size() == 1);

    // Walk the loaded region far enough away that the origin column is dropped.
    world->updateLoadedRegion(ChunkPos{100, 100});
    REQUIRE(world->find(ChunkPos{0, 0}) == nullptr);

    items.tick(*world, 1.0f / 60.0f);
    CHECK(items.size() == 0);
}

TEST_CASE("an item that falls out of the world is removed rather than falling forever") {
    auto world = floorWorld();
    ItemEntities items;

    // Below the floor, in open air all the way down.
    items.spawn(vec3{8.5f, static_cast<f32>(kWorldMinY) + 2.0f, 8.5f}, kStoneBlock, 1);
    simulate(items, *world, 5.0f);

    CHECK(items.size() == 0);
}

TEST_CASE("the inventory counts, spends and refuses") {
    Inventory inventory;

    CHECK(inventory.count(kStoneBlock) == 0);
    CHECK_FALSE(inventory.take(kStoneBlock));

    inventory.add(kStoneBlock, 3);
    inventory.add(kDirtBlock, 1);
    CHECK(inventory.count(kStoneBlock) == 3);
    CHECK(inventory.distinctBlocks() == 2);

    CHECK(inventory.take(kStoneBlock));
    CHECK(inventory.count(kStoneBlock) == 2);

    CHECK(inventory.take(kDirtBlock));
    CHECK_FALSE(inventory.take(kDirtBlock)); // Spent.
    CHECK(inventory.distinctBlocks() == 1);

    // Air is not a thing you can carry.
    inventory.add(kAirBlock, 10);
    CHECK(inventory.count(kAirBlock) == 0);
}

TEST_CASE("blocks drop what vanilla says they drop") {
    CHECK(dropOf(blockIdOf("stone")) == blockIdOf("cobblestone"));
    CHECK(dropOf(blockIdOf("grass")) == blockIdOf("dirt"));
    CHECK(dropOf(blockIdOf("oak_leaves")) == kAirBlock);

    // Everything else yields itself, which is the default and must stay so -- a
    // block added to the table without thinking about drops should still be
    // collectable rather than silently vanishing.
    CHECK(dropOf(blockIdOf("dirt")) == blockIdOf("dirt"));
    CHECK(dropOf(blockIdOf("sand")) == blockIdOf("sand"));
    CHECK(dropOf(blockIdOf("oak_log")) == blockIdOf("oak_log"));
    CHECK(dropOf(blockIdOf("cobblestone")) == blockIdOf("cobblestone"));
    CHECK(dropOf(blockIdOf("diamond_ore")) == blockIdOf("diamond_ore"));
}
