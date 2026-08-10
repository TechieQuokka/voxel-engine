#include "world/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/Generator.hpp"

#include <doctest/doctest.h>

#include <cstdlib>

using namespace mc;

TEST_CASE("generation is deterministic for a given seed") {
    const Generator a(42);
    const Generator b(42);
    const Generator other(43);

    bool differsSomewhere = false;
    for (i32 x = -64; x < 64; x += 7) {
        for (i32 z = -64; z < 64; z += 7) {
            REQUIRE(a.surfaceHeight(x, z) == b.surfaceHeight(x, z));
            if (a.surfaceHeight(x, z) != other.surfaceHeight(x, z)) {
                differsSomewhere = true;
            }
        }
    }

    // A different seed has to be a different world, not the same one.
    CHECK(differsSomewhere);
}

TEST_CASE("the surface stays inside the world") {
    const Generator generator;

    for (i32 x = -500; x < 500; x += 13) {
        for (i32 z = -500; z < 500; z += 13) {
            const i32 height = generator.surfaceHeight(x, z);
            CAPTURE(x);
            CAPTURE(z);
            REQUIRE(isValidWorldY(height));
        }
    }
}

TEST_CASE("the surface is continuous across a chunk boundary") {
    // The point of an analytic heightmap in world coordinates: a seam in the
    // terrain would mean the generator is chunk-relative somewhere, which would
    // make every neighbour test meaningless.
    const Generator generator;

    for (i32 z = -40; z < 40; ++z) {
        const i32 left = generator.surfaceHeight(kSectionSize - 1, z);
        const i32 right = generator.surfaceHeight(kSectionSize, z);
        CAPTURE(z);
        REQUIRE(std::abs(left - right) <= 3);
    }
}

TEST_CASE("a generated column is Ready and fully dirty") {
    Chunk chunk(ChunkPos{0, 0});
    const Generator generator;

    generator.generateColumn(chunk);

    CHECK(chunk.state() == ChunkState::Ready);
    // Everything needs a first mesh.
    CHECK(chunk.dirtyMask() == 0x0FFF);
}

TEST_CASE("the surface block matches the surface height") {
    Chunk chunk(ChunkPos{0, 0});
    const Generator generator;
    generator.generateColumn(chunk);

    for (i32 z = 0; z < kSectionSize; z += 5) {
        for (i32 x = 0; x < kSectionSize; x += 5) {
            const i32 surface = generator.surfaceHeight(x, z);
            CAPTURE(x);
            CAPTURE(z);

            const Section* section = chunk.sectionAt(blockToSectionCoord(surface));
            REQUIRE(section != nullptr);
            const BlockId top = section->get(x, blockToLocalCoord(surface), z);
            REQUIRE(top != kAirBlock);

            // Directly above the surface is air.
            const Section* above = chunk.sectionAt(blockToSectionCoord(surface + 1));
            REQUIRE(above != nullptr);
            REQUIRE(above->get(x, blockToLocalCoord(surface + 1), z) == kAirBlock);
        }
    }
}

TEST_CASE("sections far above and far below the surface stay uniform") {
    // This is the property that makes a 384-block world height affordable: most of
    // a column carries no index array at all.
    Chunk chunk(ChunkPos{0, 0});
    const Generator generator;
    generator.generateColumn(chunk);

    const Section& sky = chunk.sectionByIndex(Chunk::kSectionCount - 1);
    CHECK(sky.isUniform());
    CHECK(sky.isEmpty());

    const Section& deep = chunk.sectionByIndex(0); // World Y -64..-33.
    CHECK(deep.isUniform());
    CHECK(deep.uniformBlock() == kStoneBlock);

    usize uniformCount = 0;
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        if (chunk.sectionByIndex(index).isUniform()) {
            ++uniformCount;
        }
    }
    // The terrain spans a limited height band, so most sections are untouched.
    CHECK(uniformCount >= Chunk::kSectionCount - 4);
}

TEST_CASE("adjacent generated columns agree on the blocks along their seam") {
    // The check 3c depends on. If neighbouring columns disagreed here, boundary
    // face culling would be comparing against the wrong voxels.
    World world(1);
    world.updateLoadedRegion(ChunkPos{0, 0});

    const Generator generator;
    world.forEachChunk([&generator](Chunk& chunk) { generator.generateColumn(chunk); });

    for (i32 y = 20; y < 60; ++y) {
        for (i32 z = 0; z < kSectionSize; z += 4) {
            // Block x = 31 is the last in column 0, x = 32 the first in column 1.
            const BlockId inside = world.blockAt(BlockPos{kSectionSize - 1, y, z});
            const BlockId across = world.blockAt(BlockPos{kSectionSize, y, z});

            const i32 heightInside = generator.surfaceHeight(kSectionSize - 1, z);
            const i32 heightAcross = generator.surfaceHeight(kSectionSize, z);

            CAPTURE(y);
            CAPTURE(z);
            REQUIRE((inside != kAirBlock) == (y <= heightInside));
            REQUIRE((across != kAirBlock) == (y <= heightAcross));
        }
    }
}

TEST_CASE("regenerating a column produces the same result") {
    Chunk first(ChunkPos{5, -3});
    Chunk second(ChunkPos{5, -3});

    const Generator generator(7);
    generator.generateColumn(first);
    generator.generateColumn(second);
    // Twice into the same column, to check that fill() resets rather than merges.
    generator.generateColumn(second);

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const Section& a = first.sectionByIndex(index);
        const Section& b = second.sectionByIndex(index);
        CAPTURE(index);
        for (usize voxel = 0; voxel < kSectionVolume; voxel += 97) {
            REQUIRE(a.getByIndex(voxel) == b.getByIndex(voxel));
        }
    }
}

TEST_CASE("a generated column compresses to a narrow index width") {
    Chunk chunk(ChunkPos{0, 0});
    const Generator generator;
    generator.generateColumn(chunk);

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const Section& section = chunk.sectionByIndex(index);
        CAPTURE(index);
        // At most five block types exist, so nothing should need more than 4 bits.
        REQUIRE(section.storage().bitsPerIndex() <= 4);
    }
}
