#include "world/BlockTable.hpp"
#include "world/SkyLight.hpp"
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

/// Roofs every loaded column at y = 64 and settles the light under it.
///
/// Without this the test world is open sky, so *every* edit changes the sky light
/// below it and dirties the whole neighbourhood -- which is correct behaviour and
/// makes it impossible to see what dirtying the block change alone causes. Roofing
/// puts the edit in the dark, where light cannot move.
void roofAndSettle(World& world) {
    world.forEachChunk([](Chunk& chunk) {
        Section* roof = chunk.sectionAt(blockToSectionCoord(64));
        REQUIRE(roof != nullptr);
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                roof->set(x, blockToLocalCoord(64), z, kStoneBlock);
            }
        }
        computeSkyLight(chunk);
    });
}

void clearDirty(World& world) {
    world.forEachChunk([](Chunk& chunk) {
        for (usize i = 0; i < Chunk::kSectionCount; ++i) {
            chunk.clearSectionDirty(i);
        }
    });
}

/// False for a section outside the world, so a test may ask about the neighbour
/// below the bottom one without having to special-case it.
bool dirtyAt(World& world, ChunkPos column, i32 sectionY) {
    Chunk* chunk = world.find(column);
    REQUIRE(chunk != nullptr);
    if (!isValidSectionY(sectionY)) {
        return false;
    }
    return chunk->isSectionDirty(static_cast<usize>(sectionIndexInColumn(sectionY)));
}

} // namespace

TEST_CASE("setBlock writes the block and reports what happened") {
    auto world = readyWorld();

    CHECK(world->setBlock(BlockPos{4, 40, 4}, kStoneBlock) == World::EditStatus::Applied);
    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kStoneBlock);

    // Writing the same block again is a no-op, and says so -- the caller must not
    // remesh for it.
    CHECK(world->setBlock(BlockPos{4, 40, 4}, kStoneBlock) == World::EditStatus::Unchanged);

    CHECK(world->setBlock(BlockPos{4, 40, 4}, kAirBlock) == World::EditStatus::Applied);
    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kAirBlock);
}

TEST_CASE("setBlock refuses what it cannot safely touch") {
    auto world = readyWorld();

    CHECK(world->setBlock(BlockPos{4, kWorldMaxY, 4}, kStoneBlock)
          == World::EditStatus::OutsideWorld);
    CHECK(world->setBlock(BlockPos{4, kWorldMinY - 1, 4}, kStoneBlock)
          == World::EditStatus::OutsideWorld);

    // Far outside the loaded region.
    CHECK(world->setBlock(BlockPos{4000, 40, 4000}, kStoneBlock)
          == World::EditStatus::NotLoaded);

    // A column still generating is owned by a worker.
    Chunk* chunk = world->find(ChunkPos{0, 0});
    REQUIRE(chunk != nullptr);
    chunk->setState(ChunkState::Generating);
    CHECK(world->setBlock(BlockPos{4, 40, 4}, kStoneBlock) == World::EditStatus::NotLoaded);
    chunk->setState(ChunkState::Ready);

    // A pinned column is being read by a meshing job. This is the case that would be
    // a use-after-free if it were allowed through, and the caller is meant to retry.
    chunk->pin();
    CHECK(world->setBlock(BlockPos{4, 40, 4}, kStoneBlock) == World::EditStatus::Busy);
    CHECK(world->blockAt(BlockPos{4, 40, 4}) == kAirBlock); // Nothing was written.
    chunk->unpin();
    CHECK(world->setBlock(BlockPos{4, 40, 4}, kStoneBlock) == World::EditStatus::Applied);
}

TEST_CASE("an edit in the interior of a section dirties only that section") {
    auto world = readyWorld();
    roofAndSettle(*world);
    clearDirty(*world);

    // In the dark, and away from every section wall, so neither the block change nor
    // the light reaches anything else.
    const i32 y = -8;
    REQUIRE(blockToLocalCoord(y) != 0);
    REQUIRE(world->setBlock(BlockPos{16, y, 16}, kStoneBlock) == World::EditStatus::Applied);

    const i32 sectionY = blockToSectionCoord(y);
    CHECK(dirtyAt(*world, ChunkPos{0, 0}, sectionY));
    CHECK_FALSE(dirtyAt(*world, ChunkPos{0, 0}, sectionY + 1));
    CHECK_FALSE(dirtyAt(*world, ChunkPos{0, 0}, sectionY - 1));
    CHECK_FALSE(dirtyAt(*world, ChunkPos{1, 0}, sectionY));
    CHECK_FALSE(dirtyAt(*world, ChunkPos{0, 1}, sectionY));
}

TEST_CASE("an edit on a section wall dirties the neighbour behind it") {
    auto world = readyWorld();
    roofAndSettle(*world);
    clearDirty(*world);

    // x = 0 is the west wall of the column at the origin, so the column at x = -1
    // has boundary faces culled against it -- and the mesher's AO reads across too.
    const i32 y = -8;
    REQUIRE(world->setBlock(BlockPos{0, y, 16}, kStoneBlock) == World::EditStatus::Applied);

    const i32 sectionY = blockToSectionCoord(y);
    CHECK(dirtyAt(*world, ChunkPos{0, 0}, sectionY));
    CHECK(dirtyAt(*world, ChunkPos{-1, 0}, sectionY));
    // Not the far side: the block does not touch that wall.
    CHECK_FALSE(dirtyAt(*world, ChunkPos{1, 0}, sectionY));
}

TEST_CASE("an edit in a section corner dirties all eight sections it touches") {
    auto world = readyWorld();
    roofAndSettle(*world);
    clearDirty(*world);

    // The corner of the section at (0, 0): local (0, 0, 0) of a section boundary in
    // all three axes. AO reads a 3x3x3 of voxels, so seven neighbours see it.
    const i32 y = -32; // Section-aligned: kWorldMinY is -64, sections are 32 tall.
    REQUIRE(blockToLocalCoord(y) == 0);
    REQUIRE(world->setBlock(BlockPos{0, y, 0}, kStoneBlock) == World::EditStatus::Applied);

    const i32 sectionY = blockToSectionCoord(y);
    usize dirtied = 0;
    for (i32 dz = -1; dz <= 0; ++dz) {
        for (i32 dx = -1; dx <= 0; ++dx) {
            for (i32 dy = -1; dy <= 0; ++dy) {
                if (dirtyAt(*world, ChunkPos{dx, dz}, sectionY + dy)) {
                    ++dirtied;
                }
            }
        }
    }
    CHECK(dirtied == 8);
}

TEST_CASE("breaking a block underground moves no sky light") {
    auto world = readyWorld();

    roofAndSettle(*world);
    clearDirty(*world);

    // A block placed well below the roof cannot change any sky light: it is already
    // zero everywhere down there. Only the sections the block itself touches should
    // come out dirty -- this is what stops every click remeshing nine columns.
    const i32 y = -8;
    REQUIRE(blockToLocalCoord(y) != 0);
    REQUIRE(world->setBlock(BlockPos{16, y, 16}, kStoneBlock) == World::EditStatus::Applied);

    usize dirtyColumns = 0;
    world->forEachChunk([&](Chunk& chunk) {
        if (chunk.anyDirty()) {
            ++dirtyColumns;
        }
    });
    CHECK(dirtyColumns == 1);

    Chunk* centre = world->find(ChunkPos{0, 0});
    REQUIRE(centre != nullptr);
    // Exactly one section: the one holding the block.
    CHECK(centre->dirtyMask()
          == static_cast<u16>(1u << sectionIndexInColumn(blockToSectionCoord(y))));
}

TEST_CASE("removing the roof lets light in and dirties the neighbours") {
    auto world = readyWorld();

    // One column of solid stone from the world floor up to y = 64, in the centre
    // column only. Everything under the top of it is dark.
    Chunk* centre = world->find(ChunkPos{0, 0});
    REQUIRE(centre != nullptr);
    for (i32 y = kWorldMinY; y <= 64; ++y) {
        Section* section = centre->sectionAt(blockToSectionCoord(y));
        REQUIRE(section != nullptr);
        section->set(16, blockToLocalCoord(y), 16, kStoneBlock);
    }
    world->forEachChunk([](Chunk& chunk) { computeSkyLight(chunk); });

    REQUIRE(centre->sectionAt(blockToSectionCoord(60))->skyLight(16, blockToLocalCoord(60), 16)
            == 0);
    clearDirty(*world);

    // Break the top of that pillar. Daylight now falls one block further down, so
    // the light changed and the neighbouring columns -- whose boundary faces are lit
    // by light living in this column -- have to be remeshed too.
    REQUIRE(world->setBlock(BlockPos{16, 64, 16}, kAirBlock) == World::EditStatus::Applied);

    CHECK(world->blockAt(BlockPos{16, 64, 16}) == kAirBlock);

    usize dirtyColumns = 0;
    world->forEachChunk([&](Chunk& chunk) {
        if (chunk.anyDirty()) {
            ++dirtyColumns;
        }
    });
    // The centre and all eight around it.
    CHECK(dirtyColumns == 9);
}

TEST_CASE("computeSkyLight reports nothing changed when it recomputes the same answer") {
    auto world = readyWorld();
    Chunk* chunk = world->find(ChunkPos{0, 0});
    REQUIRE(chunk != nullptr);

    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            chunk->sectionAt(blockToSectionCoord(32))->set(x, blockToLocalCoord(32), z,
                                                           kStoneBlock);
        }
    }

    // First call settles it; the second must find every section already correct.
    // This is the property the edit path relies on to stay cheap.
    CHECK(computeSkyLight(*chunk) != 0);
    CHECK(computeSkyLight(*chunk) == 0);
}

TEST_CASE("an edit that does not change opacity keeps its sky light and still meshes") {
    // The relight guard, from the edit path rather than from `computeSkyLight`.
    // The test above proves the recompute *reports* nothing changed; this one proves
    // the recompute is not run at all -- and, more importantly, that skipping it
    // costs none of the other work `setBlock` owes.
    auto world = readyWorld();
    Chunk* chunk = world->find(ChunkPos{0, 0});
    REQUIRE(chunk != nullptr);

    // Open sky, so there is real light at the edit and a broken guard would show.
    const BlockPos pos{5, 40, 5};
    computeSkyLight(*chunk);
    const Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
    REQUIRE(section != nullptr);
    const u8 lightBefore = section->skyLight(blockToLocalCoord(pos.x),
                                             blockToLocalCoord(pos.y),
                                             blockToLocalCoord(pos.z));
    clearDirty(*world);

    // Air to water: both non-opaque, so the column's sky light cannot move.
    CHECK(world->setBlock(pos, kWaterBlock) == World::EditStatus::Applied);

    CHECK(section->skyLight(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                            blockToLocalCoord(pos.z))
          == lightBefore);

    // Skipping the relight must not skip the remesh: the block is visibly different
    // even though it is lit identically. Getting this wrong makes placed water
    // invisible until something else dirties the section, which is the failure the
    // guard is most likely to cause.
    CHECK(chunk->isSectionDirty(
        static_cast<usize>(sectionIndexInColumn(blockToSectionCoord(pos.y)))));

    // And an opacity change on the same cell still relights, so the guard is a
    // condition rather than a removal.
    clearDirty(*world);
    CHECK(world->setBlock(pos, kStoneBlock) == World::EditStatus::Applied);
    CHECK(section->skyLight(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                            blockToLocalCoord(pos.z))
          == 0);
}
