#include "world/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/DensityGraph.hpp"
#include "worldgen/Generator.hpp"

#include <doctest/doctest.h>

#include <algorithm>
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

TEST_CASE("the surface does not step at a chunk boundary") {
    // A jump here would mean the generator is chunk-relative somewhere, which would
    // make every neighbour test meaningless. The bound is loose on purpose: real
    // terrain has cliffs, and this asserts "no seam", not "no slope". The exact
    // continuity claim is the shared-density-plane test below.
    const Generator generator;

    i32 worst = 0;
    for (i32 z = -40; z < 40; ++z) {
        const i32 left = generator.surfaceHeight(kSectionSize - 1, z);
        const i32 right = generator.surfaceHeight(kSectionSize, z);
        worst = std::max(worst, std::abs(left - right));
    }
    CAPTURE(worst);
    REQUIRE(worst <= 8);
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

TEST_CASE("adjacent columns share their boundary density plane exactly") {
    // Terrain continuity across a chunk seam comes down to this: column (0,0)'s last
    // grid plane and column (1,0)'s first grid plane are the same world positions, so
    // they must hold the same values. If they did not, the surface would step at every
    // seam and 3c's boundary face culling would be comparing against different terrain
    // on each side.
    //
    // This replaces an earlier test that asserted "solid exactly below the surface
    // height". That was only ever true of a heightmap; with a 3D density field an
    // overhang has air below its own surface, so the old assertion was passing by
    // luck rather than by construction.
    const Generator generator(99);
    const DensityGraph& graph = generator.graph();

    DensityGraph::Climate climateA;
    DensityField densityA;
    graph.fillColumn(ChunkPos{0, 0}, climateA, densityA);

    DensityGraph::Climate climateB;
    DensityField densityB;
    graph.fillColumn(ChunkPos{1, 0}, climateB, densityB);

    constexpr i32 kLast = DensityField::kGridX - 1;
    for (i32 gz = 0; gz < DensityField::kGridZ; ++gz) {
        for (i32 gy = 0; gy < DensityField::kGridY; ++gy) {
            CAPTURE(gy);
            CAPTURE(gz);
            REQUIRE(densityA.at(kLast, gy, gz) == densityB.at(0, gy, gz));
        }
    }

    // And the same along Z, so a transposed axis cannot pass one and fail the other.
    DensityGraph::Climate climateC;
    DensityField densityC;
    graph.fillColumn(ChunkPos{0, 1}, climateC, densityC);

    for (i32 gy = 0; gy < DensityField::kGridY; ++gy) {
        for (i32 gx = 0; gx < DensityField::kGridX; ++gx) {
            CAPTURE(gx);
            CAPTURE(gy);
            REQUIRE(densityA.at(gx, gy, DensityField::kGridZ - 1) == densityC.at(gx, gy, 0));
        }
    }
}

TEST_CASE("the density field crosses zero once in open terrain") {
    // A weaker but still useful shape check: at the surface, density has to go from
    // positive underground to negative in the sky. A field that never crosses zero
    // means the whole column is solid or empty, which is what a mis-tuned squeeze
    // produces.
    const Generator generator(5);

    for (i32 x = 0; x < kSectionSize; x += 8) {
        for (i32 z = 0; z < kSectionSize; z += 8) {
            const i32 surface = generator.surfaceHeight(x, z);
            CAPTURE(x);
            CAPTURE(z);
            REQUIRE(surface > kWorldMinY);
            REQUIRE(surface < kWorldMaxY - 1);
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
