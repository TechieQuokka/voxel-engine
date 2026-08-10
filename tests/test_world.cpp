#include "world/BlockRegistry.hpp"
#include "world/World.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

usize expectedColumns(i32 renderDistance) {
    const auto side = static_cast<usize>(2 * renderDistance + 1);
    return side * side;
}

} // namespace

TEST_CASE("ChunkPosHash separates neighbouring columns") {
    const ChunkPosHash hash;

    // Chunk coordinates are small, consecutive, and usually differ in one axis, so
    // the interesting property is that adjacent positions do not collide and that
    // (a, b) differs from (b, a).
    CHECK(hash(ChunkPos{0, 0}) != hash(ChunkPos{0, 1}));
    CHECK(hash(ChunkPos{0, 0}) != hash(ChunkPos{1, 0}));
    CHECK(hash(ChunkPos{3, 7}) != hash(ChunkPos{7, 3}));
    CHECK(hash(ChunkPos{-1, 0}) != hash(ChunkPos{0, -1}));
    CHECK(hash(ChunkPos{5, -5}) == hash(ChunkPos{5, -5}));
}

TEST_CASE("the loaded region is a square of the render distance") {
    World world(2);
    CHECK(world.renderDistance() == 2);

    const auto result = world.updateLoadedRegion(ChunkPos{0, 0});

    CHECK(result.created == expectedColumns(2));
    CHECK(result.unloaded == 0);
    CHECK(world.loadedChunkCount() == expectedColumns(2));

    // The corner is inside a square region and would be outside a circular one.
    CHECK(world.find(ChunkPos{2, 2}) != nullptr);
    CHECK(world.find(ChunkPos{-2, -2}) != nullptr);
    CHECK(world.find(ChunkPos{3, 0}) == nullptr);
}

TEST_CASE("a second call at the same centre creates nothing") {
    World world(3);
    world.updateLoadedRegion(ChunkPos{10, -4});

    const auto again = world.updateLoadedRegion(ChunkPos{10, -4});
    CHECK(again.created == 0);
    CHECK(again.unloaded == 0);
    CHECK(world.loadedChunkCount() == expectedColumns(3));
}

TEST_CASE("moving the centre loads and unloads only the difference") {
    World world(2);
    world.updateLoadedRegion(ChunkPos{0, 0});

    // One step along X: a 5x5 region sheds one column of 5 and gains another.
    const auto moved = world.updateLoadedRegion(ChunkPos{1, 0});

    CHECK(moved.created == 5);
    CHECK(moved.unloaded == 5);
    CHECK(world.loadedChunkCount() == expectedColumns(2));
    CHECK(world.find(ChunkPos{-2, 0}) == nullptr); // Shed.
    CHECK(world.find(ChunkPos{3, 0}) != nullptr);  // Gained.
}

TEST_CASE("columns work at negative coordinates") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{-100, -100});

    CHECK(world.loadedChunkCount() == expectedColumns(1));
    const Chunk* chunk = world.find(ChunkPos{-100, -100});
    REQUIRE(chunk != nullptr);
    CHECK(chunk->position() == ChunkPos{-100, -100});
}

TEST_CASE("a column being generated is never unloaded from under a worker") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    Chunk* chunk = world.find(ChunkPos{-1, -1});
    REQUIRE(chunk != nullptr);
    chunk->setState(ChunkState::Generating);

    // Move far enough that every old column falls outside the new region.
    const auto moved = world.updateLoadedRegion(ChunkPos{50, 50});

    CHECK(moved.retained == 1);
    CHECK(world.find(ChunkPos{-1, -1}) != nullptr);

    // Once the job finishes, the next update drops it.
    world.find(ChunkPos{-1, -1})->setState(ChunkState::Ready);
    const auto after = world.updateLoadedRegion(ChunkPos{50, 50});
    CHECK(after.retained == 0);
    CHECK(after.unloaded == 1);
    CHECK(world.find(ChunkPos{-1, -1}) == nullptr);
}

TEST_CASE("blockAt reads through to the section that owns the voxel") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    Chunk* chunk = world.find(ChunkPos{0, 0});
    REQUIRE(chunk != nullptr);

    Section* section = chunk->sectionAt(0); // World Y 0..31.
    REQUIRE(section != nullptr);
    section->set(5, 6, 7, kStoneBlock);

    CHECK(world.blockAt(BlockPos{5, 6, 7}) == kStoneBlock);
    CHECK(world.blockAt(BlockPos{5, 6, 8}) == kAirBlock);
}

TEST_CASE("blockAt is air outside the world and outside loaded columns") {
    World world(0);
    world.updateLoadedRegion(ChunkPos{0, 0});

    CHECK(world.blockAt(BlockPos{0, kWorldMaxY, 0}) == kAirBlock);
    CHECK(world.blockAt(BlockPos{0, kWorldMinY - 1, 0}) == kAirBlock);
    CHECK(world.blockAt(BlockPos{10000, 0, 10000}) == kAirBlock);
}

TEST_CASE("blockAt handles negative coordinates without straddling a section") {
    World world(2);
    world.updateLoadedRegion(ChunkPos{-1, -1});

    // Block -1 belongs to section -1 at local coordinate 31, which is the case
    // plain division would get wrong.
    Chunk* chunk = world.find(ChunkPos{-1, -1});
    REQUIRE(chunk != nullptr);
    Section* section = chunk->sectionAt(-1);
    REQUIRE(section != nullptr);
    section->set(31, 31, 31, kSandBlock);

    CHECK(world.blockAt(BlockPos{-1, -1, -1}) == kSandBlock);
}

TEST_CASE("the neighbourhood centre is the section itself") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    const SectionPos center{0, 0, 0};
    const SectionNeighbourhood hood = world.neighbourhood(center);

    CHECK(hood.center() == world.sectionAt(center));
    CHECK(hood.center() != nullptr);
}

TEST_CASE("the neighbourhood gathers all 27 sections when they are loaded") {
    World world(2);
    world.updateLoadedRegion(ChunkPos{0, 0});

    // Section Y 0 has a section above and below inside the world, so all 27 exist.
    const SectionNeighbourhood hood = world.neighbourhood(SectionPos{0, 0, 0});

    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dy = -1; dy <= 1; ++dy) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                CAPTURE(dx);
                CAPTURE(dy);
                CAPTURE(dz);
                CHECK(hood.at(dx, dy, dz) != nullptr);
            }
        }
    }
}

TEST_CASE("the neighbourhood is null above the sky and below bedrock") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    const SectionNeighbourhood top = world.neighbourhood(SectionPos{0, kMaxSectionY - 1, 0});
    CHECK(top.center() != nullptr);
    CHECK(top.at(0, 1, 0) == nullptr);  // Above the world.
    CHECK(top.at(0, -1, 0) != nullptr);

    const SectionNeighbourhood bottom = world.neighbourhood(SectionPos{0, kMinSectionY, 0});
    CHECK(bottom.center() != nullptr);
    CHECK(bottom.at(0, -1, 0) == nullptr); // Below the world.
    CHECK(bottom.at(0, 1, 0) != nullptr);
}

TEST_CASE("the neighbourhood is null where a column is not loaded") {
    World world(0); // A single column, so every horizontal neighbour is missing.
    world.updateLoadedRegion(ChunkPos{0, 0});

    const SectionNeighbourhood hood = world.neighbourhood(SectionPos{0, 0, 0});

    CHECK(hood.center() != nullptr);
    CHECK(hood.at(1, 0, 0) == nullptr);
    CHECK(hood.at(-1, 0, 0) == nullptr);
    CHECK(hood.at(0, 0, 1) == nullptr);
    CHECK(hood.at(1, 0, 1) == nullptr);
    CHECK(hood.at(0, 1, 0) != nullptr); // Same column, so still there.
}

TEST_CASE("neighbourhood blockAt crosses section boundaries in every direction") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    // Plant a block in each of the six face-adjacent sections, at the voxel
    // touching the centre section, then read it through the centre's coordinates.
    const auto plant = [&world](SectionPos pos, i32 x, i32 y, i32 z, BlockId block) {
        Chunk* chunk = world.find(ChunkPos{pos.x, pos.z});
        REQUIRE(chunk != nullptr);
        Section* section = chunk->sectionAt(pos.y);
        REQUIRE(section != nullptr);
        section->set(x, y, z, block);
    };

    plant(SectionPos{-1, 0, 0}, 31, 5, 5, kStoneBlock); // x = -1
    plant(SectionPos{1, 0, 0}, 0, 5, 5, kDirtBlock);    // x = 32
    plant(SectionPos{0, -1, 0}, 5, 31, 5, kSandBlock);  // y = -1
    plant(SectionPos{0, 1, 0}, 5, 0, 5, kGrassBlock);   // y = 32
    plant(SectionPos{0, 0, -1}, 5, 5, 31, kStoneBlock); // z = -1
    plant(SectionPos{0, 0, 1}, 5, 5, 0, kDirtBlock);    // z = 32

    const SectionNeighbourhood hood = world.neighbourhood(SectionPos{0, 0, 0});

    CHECK(hood.blockAt(-1, 5, 5) == kStoneBlock);
    CHECK(hood.blockAt(32, 5, 5) == kDirtBlock);
    CHECK(hood.blockAt(5, -1, 5) == kSandBlock);
    CHECK(hood.blockAt(5, 32, 5) == kGrassBlock);
    CHECK(hood.blockAt(5, 5, -1) == kStoneBlock);
    CHECK(hood.blockAt(5, 5, 32) == kDirtBlock);
}

TEST_CASE("neighbourhood blockAt reaches the diagonal sections AO needs") {
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    // The corner-adjacent section. AO samples this, which is why the
    // neighbourhood is 27 sections and not 6.
    Chunk* corner = world.find(ChunkPos{-1, -1});
    REQUIRE(corner != nullptr);
    Section* section = corner->sectionAt(-1);
    REQUIRE(section != nullptr);
    section->set(31, 31, 31, kStoneBlock);

    const SectionNeighbourhood hood = world.neighbourhood(SectionPos{0, 0, 0});
    CHECK(hood.blockAt(-1, -1, -1) == kStoneBlock);
}

TEST_CASE("neighbourhood blockAt is air beyond one section in any direction") {
    World world(3);
    world.updateLoadedRegion(ChunkPos{0, 0});

    // Loaded, but outside the 3x3x3 block, so it must not be reachable -- the
    // mesher relies on the neighbourhood being a closed view of what it may read.
    Chunk* far = world.find(ChunkPos{2, 0});
    REQUIRE(far != nullptr);
    Section* section = far->sectionAt(0);
    REQUIRE(section != nullptr);
    section->set(0, 0, 0, kStoneBlock);

    const SectionNeighbourhood hood = world.neighbourhood(SectionPos{0, 0, 0});
    CHECK(hood.blockAt(64, 0, 0) == kAirBlock);
    CHECK(hood.blockAt(-33, 0, 0) == kAirBlock);
}

TEST_CASE("clear drops everything") {
    World world(2);
    world.updateLoadedRegion(ChunkPos{0, 0});
    REQUIRE(world.loadedChunkCount() > 0);

    world.clear();
    CHECK(world.loadedChunkCount() == 0);
    CHECK(world.find(ChunkPos{0, 0}) == nullptr);
}

TEST_CASE("forEachChunk visits every loaded column once") {
    World world(2);
    world.updateLoadedRegion(ChunkPos{0, 0});

    usize visited = 0;
    world.forEachChunk([&visited](const Chunk&) { ++visited; });
    CHECK(visited == expectedColumns(2));
}

TEST_CASE("shrinking the render distance unloads the outer rings") {
    World world(3);
    world.updateLoadedRegion(ChunkPos{0, 0});
    REQUIRE(world.loadedChunkCount() == expectedColumns(3));

    world.setRenderDistance(1);
    const auto result = world.updateLoadedRegion(ChunkPos{0, 0});

    CHECK(result.created == 0);
    CHECK(result.unloaded == expectedColumns(3) - expectedColumns(1));
    CHECK(world.loadedChunkCount() == expectedColumns(1));
}
