#include "render/CharacterRenderer.hpp"
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

/// The player standing with their feet at `feet`, built from the engine's **real**
/// dimensions and reach rather than from numbers chosen to make a test pass.
///
/// **That distinction is the whole reason this helper exists.** Pickup shipped
/// broken for four play sessions with these cases passing, because each of them
/// picked its own reference point and its own radius -- so nothing ever asserted
/// anything about `kPickupRadius` against the height of an actual player. The tests
/// covered `ItemEntities`; the bug was in the two constants the caller combined.
ItemEntities::PickupVolume standingAt(const vec3& feet) {
    return ItemEntities::PickupVolume{feet, CharacterRenderer::kHeight,
                                      ItemEntities::kPickupRadius};
}

/// Where the floor's top face is in `floorWorld`, and therefore where a player
/// standing on it has their feet.
constexpr f32 kFloorTop = 64.0f;

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
    CHECK(items.collect(standingAt(vec3{8.5f, kFloorTop, 8.5f})).empty());
    CHECK(items.size() == 1);

    simulate(items, *world, 1.0f);

    const auto picked = items.collect(standingAt(vec3{8.5f, kFloorTop, 8.5f}));
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

    const auto picked = items.collect(standingAt(vec3{8.5f, kFloorTop, 8.5f}));
    REQUIRE(picked.size() == 1);
    CHECK(picked[0].block == kStoneBlock);
    CHECK(items.size() == 1); // The far one is untouched.
}

TEST_CASE("an item lying on the ground the player stands on is picked up") {
    // **The regression test for the bug that shipped in 7.10 and survived four play
    // sessions.** The break-drop-collect-place loop was documented as closed and one
    // of its four steps had never once worked: pickup measured a 1.4 sphere from the
    // eye at 1.62, and an item rests at 0.12, so the single most ordinary case in the
    // game -- walk over what you just broke -- was 1.50 away and always out of range.
    //
    // Nothing here is a chosen number. The volume is the player's real dimensions and
    // the item lands wherever the physics puts it.
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 70.0f, 8.5f}, kStoneBlock, 1);
    simulate(items, *world, 3.0f);
    REQUIRE(items.size() == 1);

    const auto picked = items.collect(standingAt(vec3{8.5f, kFloorTop, 8.5f}));
    REQUIRE(picked.size() == 1);
    CHECK(picked[0].block == kStoneBlock);
    CHECK(items.size() == 0);
}

TEST_CASE("the pickup volume is measured from the body, not from the eye") {
    // The same item, reached for the way the broken code reached for it: a sphere
    // whose centre is the eye. **It has to still fail.** That is what keeps the fix
    // honest -- if someone "fixes" this again by enlarging the radius until the eye
    // can reach the floor, this case goes red instead of the bug coming back.
    auto world = floorWorld();
    ItemEntities items;

    items.spawn(vec3{8.5f, 70.0f, 8.5f}, kStoneBlock, 1);
    simulate(items, *world, 3.0f);
    REQUIRE(items.size() == 1);

    const vec3 feet{8.5f, kFloorTop, 8.5f};
    const vec3 eye = feet + vec3{0.0f, CharacterRenderer::kEyeHeight, 0.0f};
    const vec3 item = items.items().front().position;

    // A point volume at the eye: zero height, so the clamp cannot save it.
    const ItemEntities::PickupVolume atEye{eye, 0.0f, ItemEntities::kPickupRadius};
    CHECK(ItemEntities::distanceSquaredTo(atEye, item)
          > ItemEntities::kPickupRadius * ItemEntities::kPickupRadius);

    // The same item against the real volume contributes **nothing** vertically,
    // because it rests between the feet and the top of the head -- so what is left is
    // the horizontal distance alone. Stated as an equality against that distance
    // rather than as a small number, since the spawn scatter means the item is not
    // exactly under the player and a tolerance would be hiding the thing being
    // measured.
    const ItemEntities::PickupVolume body = standingAt(feet);
    const f32 horizontal = (item.x - feet.x) * (item.x - feet.x)
                           + (item.z - feet.z) * (item.z - feet.z);
    CHECK(ItemEntities::distanceSquaredTo(body, item) == doctest::Approx(horizontal));
    CHECK(ItemEntities::distanceSquaredTo(body, item)
          < ItemEntities::kPickupRadius * ItemEntities::kPickupRadius);
}

TEST_CASE("pickup reach falls off with distance rather than being unbounded") {
    // The fix must not become "collect everything". A stack one block down is still
    // in reach -- that is walking over a ledge -- and one three blocks down is not.
    const vec3 feet{8.5f, kFloorTop, 8.5f};
    const ItemEntities::PickupVolume body = standingAt(feet);
    const f32 limit = ItemEntities::kPickupRadius * ItemEntities::kPickupRadius;

    CHECK(ItemEntities::distanceSquaredTo(body, vec3{8.5f, kFloorTop - 0.88f, 8.5f})
          < limit);
    CHECK(ItemEntities::distanceSquaredTo(body, vec3{8.5f, kFloorTop - 3.0f, 8.5f})
          > limit);

    // And a stack level with the head is in reach, which the old point test at the
    // eye also got right and is the one case it did.
    CHECK(ItemEntities::distanceSquaredTo(
              body, vec3{8.5f, kFloorTop + CharacterRenderer::kEyeHeight, 8.5f})
          < limit);

    // Horizontally it is the radius and nothing else: the clamp must not widen it.
    CHECK(ItemEntities::distanceSquaredTo(body, vec3{8.5f + 1.2f, kFloorTop + 1.0f, 8.5f})
          < limit);
    CHECK(ItemEntities::distanceSquaredTo(body, vec3{8.5f + 2.0f, kFloorTop + 1.0f, 8.5f})
          > limit);
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

TEST_CASE("a full inventory leaves the item on the ground") {
    auto world = floorWorld();
    ItemEntities items;
    items.spawn(vec3{8.5f, 65.0f, 8.5f}, kStoneBlock, 40);
    simulate(items, *world, 1.0f);

    // An acceptor with no room at all. Before stack limits existed the item was
    // removed whether the caller could take it or not, which with a bounded
    // inventory would silently delete it.
    items.collectInto(standingAt(vec3{8.5f, kFloorTop, 8.5f}),
                      [](BlockId, u32 count) { return count; });
    CHECK(items.size() == 1);

    // Room for half of it: the rest stays, rather than the whole stack going or
    // the whole stack staying.
    items.collectInto(standingAt(vec3{8.5f, kFloorTop, 8.5f}),
                      [](BlockId, u32 count) { return count / 2; });
    REQUIRE(items.size() == 1);
    CHECK(items.items()[0].count == 20);

    items.collectInto(standingAt(vec3{8.5f, kFloorTop, 8.5f}),
                      [](BlockId, u32) { return 0u; });
    CHECK(items.size() == 0);
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
