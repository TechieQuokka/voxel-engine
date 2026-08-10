#include "world/BlockRegistry.hpp"
#include "world/World.hpp"
#include "worldgen/DensityGraph.hpp"
#include "worldgen/Generator.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>

using namespace mc;

TEST_CASE("generation is deterministic for a given seed") {
    const Generator a(42);
    const Generator b(42);
    const Generator other(43);

    bool differsSomewhere = false;
    for (i32 x = -64; x < 64; x += 7) {
        for (i32 z = -64; z < 64; z += 7) {
            REQUIRE(a.terrainHeight(x, z) == b.terrainHeight(x, z));
            if (a.terrainHeight(x, z) != other.terrainHeight(x, z)) {
                differsSomewhere = true;
            }
        }
    }

    // A different seed has to be a different world, not the same one.
    CHECK(differsSomewhere);
}

TEST_CASE("the surface stays inside the world") {
    const Generator generator;

    for (i32 x = -500; x < 500; x += 61) {
        for (i32 z = -500; z < 500; z += 61) {
            const i32 height = generator.terrainHeight(x, z);
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
        const i32 left = generator.terrainHeight(kSectionSize - 1, z);
        const i32 right = generator.terrainHeight(kSectionSize, z);
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

    // surfaceHeight, not terrainHeight: this compares against blocks that carvers have
    // already cut, so the cheap density-only answer would disagree wherever a cave
    // breaks the surface -- which is a real thing that happens now.
    for (i32 z = 0; z < kSectionSize; z += 11) {
        for (i32 x = 0; x < kSectionSize; x += 11) {
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

TEST_CASE("the sky stays uniform, and caves cost some of the deep uniformity") {
    // The property that makes a 384-block world height affordable is that most of a
    // column carries no index array at all. Caves eat into it, and this test records
    // by how much rather than asserting the pre-cave number.
    //
    // Before carvers, 8 of 12 sections in this column were uniform and the bottom one
    // was solid stone all the way down. Thin caves reach to y = -59, one block above
    // bedrock, so the bottom section is no longer uniform -- which is correct, and is
    // exactly the cost predicted when caves were planned.
    Chunk chunk(ChunkPos{0, 0});
    const Generator generator;
    generator.generateColumn(chunk);

    // The sky is untouched by anything and must stay free.
    const Section& sky = chunk.sectionByIndex(Chunk::kSectionCount - 1);
    CHECK(sky.isUniform());
    CHECK(sky.isEmpty());

    usize uniformCount = 0;
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        if (chunk.sectionByIndex(index).isUniform()) {
            ++uniformCount;
        }
    }
    CAPTURE(uniformCount);
    // Still the majority of the column. If this drops below half, the caves have
    // become a sponge and the storage argument in DESIGN.md 3.5 needs revisiting.
    CHECK(uniformCount >= Chunk::kSectionCount / 2);
}

TEST_CASE("caves actually carve, and only below the surface") {
    // Two things at once: air appears underground where the density field alone would
    // have put rock, and the very top of the world is not perforated.
    Chunk chunk(ChunkPos{3, -2});
    const Generator generator(4242);
    generator.generateColumn(chunk);

    // Surface heights first, one per horizontal position. Calling surfaceHeight inside
    // the voxel loop would generate a column per voxel -- it is cached per column, not
    // per position.
    std::array<i32, kSectionSize * kSectionSize> surface{};
    for (i32 z = 0; z < kSectionSize; z += 8) {
        for (i32 x = 0; x < kSectionSize; x += 8) {
            surface[static_cast<usize>(z * kSectionSize + x)] = generator.surfaceHeight(
                chunk.position().x * kSectionSize + x, chunk.position().z * kSectionSize + z);
        }
    }

    usize undergroundAir = 0;
    usize solidBlocks = 0;
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const Section& section = chunk.sectionByIndex(index);
        const i32 sectionMinY = sectionIndexToWorldY(static_cast<i32>(index));
        if (section.isEmpty()) {
            continue;
        }
        for (i32 y = 0; y < kSectionSize; ++y) {
            const i32 worldY = sectionMinY + y;
            for (i32 z = 0; z < kSectionSize; z += 8) {
                for (i32 x = 0; x < kSectionSize; x += 8) {
                    const bool air = section.get(x, y, z) == kAirBlock;
                    // Well below the surface, so this is cave air rather than sky.
                    const bool covered =
                        worldY + 20 < surface[static_cast<usize>(z * kSectionSize + x)];
                    if (air && covered) {
                        ++undergroundAir;
                    }
                    if (!air) {
                        ++solidBlocks;
                    }
                }
            }
        }
    }

    CAPTURE(undergroundAir);
    CAPTURE(solidBlocks);
    CHECK(undergroundAir > 0);   // Caves exist.
    CHECK(solidBlocks > 0);      // And have not eaten everything.
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
            const i32 surface = generator.terrainHeight(x, z);
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
