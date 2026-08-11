#include "world/BlockTable.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

/// A world with one loaded column at the origin, marked Ready so edits are allowed.
/// Every section starts as uniform air.
std::unique_ptr<World> emptyWorld(i32 renderDistance = 1) {
    auto world = std::make_unique<World>(renderDistance);
    world->updateLoadedRegion(ChunkPos{0, 0});
    world->forEachChunk([](Chunk& chunk) { chunk.setState(ChunkState::Ready); });
    return world;
}

/// Writes a block without going through setBlock, so raycast tests are not also
/// testing the edit path.
void poke(World& world, BlockPos pos, BlockId block) {
    Chunk* chunk = world.find(toChunkPos(pos));
    REQUIRE(chunk != nullptr);
    Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
    REQUIRE(section != nullptr);
    section->set(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y), blockToLocalCoord(pos.z),
                 block);
}

} // namespace

TEST_CASE("a ray finds the nearest block and the face it entered through") {
    auto world = emptyWorld();
    poke(*world, BlockPos{5, 10, 5}, kStoneBlock);

    // Straight along +x from inside the block's own row.
    const auto hit = raycast(*world, vec3{0.5f, 10.5f, 5.5f}, vec3{1.0f, 0.0f, 0.0f}, 32.0f);
    REQUIRE(hit.has_value());

    CHECK(hit->block == BlockPos{5, 10, 5});
    CHECK(hit->face == Face::NegX);
    // Entered through -x, so the placement cell is one step back along -x.
    CHECK(hit->adjacent == BlockPos{4, 10, 5});
    CHECK(hit->distance == doctest::Approx(4.5f));
}

TEST_CASE("the entry face is reported for every axis and both directions") {
    auto world = emptyWorld();

    struct Case {
        vec3 direction;
        BlockPos target;
        Face face;
    };

    // The block sits at the origin cell of each ray, six blocks out.
    const Case cases[] = {
        {{1.0f, 0.0f, 0.0f},  {6, 10, 5},  Face::NegX},
        {{-1.0f, 0.0f, 0.0f}, {-6, 10, 5}, Face::PosX},
        {{0.0f, 1.0f, 0.0f},  {5, 16, 5},  Face::NegY},
        {{0.0f, -1.0f, 0.0f}, {5, 4, 5},   Face::PosY},
        {{0.0f, 0.0f, 1.0f},  {5, 10, 11}, Face::NegZ},
        {{0.0f, 0.0f, -1.0f}, {5, 10, -1}, Face::PosZ},
    };

    for (const Case& c : cases) {
        auto fresh = emptyWorld();
        poke(*fresh, c.target, kStoneBlock);

        const auto hit = raycast(*fresh, vec3{5.5f, 10.5f, 5.5f}, c.direction, 32.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->block == c.target);
        CHECK(hit->face == c.face);
        // The placement cell is always between the eye and the block.
        CHECK(hit->adjacent == offsetByFace(c.target, c.face));
    }
}

TEST_CASE("a diagonal ray does not tunnel through a one-block wall") {
    auto world = emptyWorld();

    // A solid plane at x = 8, one block thick. A fixed-step sampler with a step
    // larger than the cell would step straight over this; the DDA cannot.
    for (i32 y = 0; y < 20; ++y) {
        for (i32 z = 0; z < 20; ++z) {
            poke(*world, BlockPos{8, y, z}, kStoneBlock);
        }
    }

    // Deliberately awkward directions -- nothing axis-aligned, nothing that lands
    // on a grid line.
    const vec3 directions[] = {
        {1.0f, 0.31f, 0.17f},
        {1.0f, -0.44f, 0.63f},
        {1.0f, 0.97f, -0.29f},
        {1.0f, 0.03f, 0.99f},
    };

    for (const vec3& direction : directions) {
        const auto hit = raycast(*world, vec3{0.5f, 10.5f, 5.5f}, direction, 40.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->block.x == 8);
        CHECK(hit->face == Face::NegX);
    }
}

TEST_CASE("a ray stops at its reach and finds nothing in empty air") {
    auto world = emptyWorld();
    poke(*world, BlockPos{20, 10, 5}, kStoneBlock);

    // The block is 15 blocks away; a five-block reach must not see it.
    CHECK_FALSE(raycast(*world, vec3{5.5f, 10.5f, 5.5f}, vec3{1.0f, 0.0f, 0.0f}, 5.0f)
                    .has_value());
    CHECK(raycast(*world, vec3{5.5f, 10.5f, 5.5f}, vec3{1.0f, 0.0f, 0.0f}, 20.0f).has_value());

    // Nothing at all in the other direction.
    CHECK_FALSE(raycast(*world, vec3{5.5f, 10.5f, 5.5f}, vec3{-1.0f, 0.0f, 0.0f}, 20.0f)
                    .has_value());
}

TEST_CASE("the block the eye is inside is not a hit") {
    auto world = emptyWorld();
    poke(*world, BlockPos{5, 10, 5}, kStoneBlock);
    poke(*world, BlockPos{9, 10, 5}, kStoneBlock);

    // Standing inside the first block. Reporting it would let a click break whatever
    // the player's head is buried in, at distance zero.
    const auto hit = raycast(*world, vec3{5.5f, 10.5f, 5.5f}, vec3{1.0f, 0.0f, 0.0f}, 32.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->block == BlockPos{9, 10, 5});
}

TEST_CASE("a degenerate ray is rejected rather than looping") {
    auto world = emptyWorld();
    poke(*world, BlockPos{5, 10, 5}, kStoneBlock);

    CHECK_FALSE(raycast(*world, vec3{0.5f, 10.5f, 5.5f}, vec3{0.0f, 0.0f, 0.0f}, 32.0f)
                    .has_value());
    CHECK_FALSE(raycast(*world, vec3{0.5f, 10.5f, 5.5f}, vec3{1.0f, 0.0f, 0.0f}, 0.0f)
                    .has_value());
    CHECK_FALSE(raycast(*world, vec3{0.5f, 10.5f, 5.5f}, vec3{1.0f, 0.0f, 0.0f}, -1.0f)
                    .has_value());
}

TEST_CASE("a ray leaving the world's vertical range terminates") {
    auto world = emptyWorld();

    // Straight up, out of the top of the world, with a reach far longer than the
    // world is tall. This must end rather than march forever.
    CHECK_FALSE(raycast(*world, vec3{5.5f, 300.5f, 5.5f}, vec3{0.0f, 1.0f, 0.0f}, 1000.0f)
                    .has_value());
    CHECK_FALSE(raycast(*world, vec3{5.5f, -60.5f, 5.5f}, vec3{0.0f, -1.0f, 0.0f}, 1000.0f)
                    .has_value());

    // But one that re-enters from above still finds what is below it.
    poke(*world, BlockPos{5, 100, 5}, kStoneBlock);
    const auto hit = raycast(*world, vec3{5.5f, 330.0f, 5.5f}, vec3{0.0f, -1.0f, 0.0f}, 1000.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->block == BlockPos{5, 100, 5});
    CHECK(hit->face == Face::PosY);
}

TEST_CASE("an unloaded column reads as air rather than as a hit") {
    auto world = emptyWorld(0); // Only the column at the origin.

    // Aimed into a column that was never loaded. Reporting a hit there would let the
    // player break a block that has not been generated.
    CHECK_FALSE(raycast(*world, vec3{16.5f, 10.5f, 16.5f}, vec3{1.0f, 0.0f, 0.0f}, 64.0f)
                    .has_value());
}
