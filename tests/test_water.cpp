#include "mesh/BinaryGreedyMesher.hpp"
#include "mesh/CulledMesher.hpp"
#include "world/BlockTable.hpp"
#include "world/Section.hpp"
#include "world/World.hpp"
#include "worldgen/Generator.hpp"

#include <doctest/doctest.h>

#include <array>
#include <memory>

using namespace mc;

namespace {

constexpr GreedyMeshOptions kNoAo{.ambientOcclusion = false, .aoAwareMerging = false};

/// Counts the quads whose material is the water layer.
usize waterQuads(const ChunkMesh& mesh) {
    const u16 layer = layerOf("water");
    usize count = 0;
    for (const Quad& quad : mesh.quads) {
        if (quad.material() == layer) {
            ++count;
        }
    }
    return count;
}

/// Fills y in [0, height) with `block`.
void fillTo(Section& section, i32 height, BlockId block) {
    for (i32 y = 0; y < height; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                section.set(x, y, z, block);
            }
        }
    }
}

} // namespace

TEST_CASE("a slab of water meshes as a surface, not as a stack of cubes") {
    Section section;
    fillTo(section, 8, kWaterBlock);

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    // **The property water exists to have.** 32x32x8 water cubes have 8,192 internal
    // faces between them; every one must be culled against its own kind. What is
    // left is the outside of the slab, and with no neighbouring sections supplied
    // that is the top, the bottom and the four sides.
    //
    // **Ten rather than six, and the extra four are the surface.** Each wall splits
    // in two: the seven submerged rows are full-height and merge, and the top row
    // cannot join them because its upper edge is drawn a ninth of a block lower --
    // that is where the water surface is. The split is the feature rather than a
    // lost merge, and it is bounded at one extra quad per wall however deep the
    // water gets. See Quad.hpp for what carries the height.
    CHECK(waterQuads(mesh) == 10);
    CHECK(mesh.quadCount() == 10);
}

TEST_CASE("water and stone hide each other's faces") {
    Section section;
    fillTo(section, 4, kStoneBlock);
    for (i32 y = 4; y < 12; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                section.set(x, y, z, kWaterBlock);
            }
        }
    }

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    // The stone's top face is under water, and water is not opaque, so the stone
    // still draws it -- that is the sea bed you see through the surface. The water's
    // *bottom* face rests on the stone and must not be drawn: it is hidden, and
    // drawing it would put a second translucent layer over the sea bed and darken it.
    const std::array<usize, kFaceCount> waterByFace = [&] {
        std::array<usize, kFaceCount> counts{};
        const u16 layer = layerOf("water");
        for (const Quad& quad : mesh.quads) {
            if (quad.material() == layer) {
                ++counts[static_cast<usize>(quad.face())];
            }
        }
        return counts;
    }();

    CHECK(waterByFace[static_cast<usize>(Face::PosY)] == 1); // the surface
    CHECK(waterByFace[static_cast<usize>(Face::NegY)] == 0); // resting on the bed
}

TEST_CASE("the opaque and translucent halves are separated, in that order") {
    Section section;
    fillTo(section, 4, kStoneBlock);
    for (i32 y = 4; y < 8; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                section.set(x, y, z, kWaterBlock);
            }
        }
    }

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    REQUIRE(mesh.opaqueQuads > 0);
    REQUIRE(mesh.translucentQuads() > 0);
    CHECK(mesh.opaqueQuads + mesh.translucentQuads() == mesh.quadCount());

    // The renderer draws [0, opaqueQuads) in the first pass and the rest in the
    // second, so the split has to be a real partition rather than a hint.
    const u16 water = layerOf("water");
    for (usize i = 0; i < mesh.quadCount(); ++i) {
        const bool isWater = mesh.quads[i].material() == water;
        CHECK(isWater == (i >= mesh.opaqueQuads));
    }
}

TEST_CASE("a section with no water leaves the split at the end") {
    Section section;
    fillTo(section, 8, kStoneBlock);

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    CHECK(mesh.opaqueQuads == mesh.quadCount());
    CHECK(mesh.translucentQuads() == 0);
}

TEST_CASE("both meshers agree about water") {
    // The culled mesher is the oracle, and it has to stay one now that there are two
    // passes. It emits per face, the greedy one merges -- so the quad counts differ
    // and the *covered area* must not.
    Section section;
    fillTo(section, 3, kStoneBlock);
    for (i32 y = 3; y < 9; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                section.set(x, y, z, kWaterBlock);
            }
        }
    }

    ChunkMesh greedy;
    ChunkMesh reference;
    meshSectionGreedy(section, greedy, kNoAo);
    meshSectionCulled(section, reference);

    const auto area = [](const ChunkMesh& mesh, bool water) {
        const u16 layer = layerOf("water");
        usize total = 0;
        for (const Quad& quad : mesh.quads) {
            if ((quad.material() == layer) == water) {
                total += static_cast<usize>(quad.width()) * quad.height();
            }
        }
        return total;
    };

    CHECK(area(greedy, true) == area(reference, true));
    CHECK(area(greedy, false) == area(reference, false));
    CHECK(greedy.quadCount() < reference.quadCount()); // merging did something
}

TEST_CASE("water in a neighbour culls the faces on the boundary") {
    Section centre;
    Section neighbour;
    for (i32 y = 0; y < kSectionSize; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                centre.set(x, y, z, kWaterBlock);
                neighbour.set(x, y, z, kWaterBlock);
            }
        }
    }

    SectionNeighbourhood hood;
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dy = -1; dy <= 1; ++dy) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                hood.set(dx, dy, dz,
                         (dx == 0 && dy == 0 && dz == 0) ? &centre : &neighbour);
            }
        }
    }

    ChunkMesh mesh;
    meshSectionGreedy(hood, mesh, kNoAo);

    // A section of ocean entirely surrounded by more ocean draws nothing at all.
    // Without the fluid shell in `decodeNeighbourhood` this would emit a wall of
    // quads on all six sides -- a visible 32-block grid inside every sea.
    CHECK(mesh.quadCount() == 0);
}

TEST_CASE("the generated sea obeys all three of its rules at once") {
    // **Counters rather than a CHECK per voxel.** A column is 393,216 voxels and this
    // walks several of them; asserting inside the loop put 650,000 doctest assertions
    // into the suite and doubled the asan run for no extra information. A failing
    // count says exactly as much and names the rule that broke.
    Generator generator{1234};

    const auto blockAt = [](const Chunk& chunk, i32 x, i32 y, i32 z) {
        const Section* section = chunk.sectionAt(blockToSectionCoord(y));
        return section == nullptr ? kAirBlock
                                  : section->get(x, blockToLocalCoord(y), z);
    };

    usize aboveSeaLevel = 0; ///< water higher than the flood ever starts
    usize hangingInAir = 0;  ///< water with air under it
    usize belowTheBed = 0;   ///< water under the first solid: a flooded cave
    usize waterFound = 0;
    usize columnsSearched = 0;

    // **Walk outward until an ocean turns up.** The origin column is not guaranteed
    // to have one -- with seed 1234 it does not, and an earlier version of this test
    // passed every rule vacuously because of it. The `waterFound` check below is what
    // caught that, and it stays for the same reason.
    for (i32 step = 0; step < 24 && waterFound == 0; ++step) {
        Chunk chunk{ChunkPos{step * 3, step * 5}};
        generator.generateColumn(chunk);
        ++columnsSearched;

        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                bool seenWater = false;
                bool pastBed = false;

                for (i32 y = kWorldMaxY - 1; y > kWorldMinY; --y) {
                    const BlockId block = blockAt(chunk, x, y, z);

                    if (isFluid(block)) {
                        ++waterFound;
                        seenWater = true;
                        aboveSeaLevel += y > kSeaLevel ? 1u : 0u;
                        belowTheBed += pastBed ? 1u : 0u;
                        hangingInAir += blockAt(chunk, x, y - 1, z) == kAirBlock ? 1u : 0u;
                    } else if (block != kAirBlock && seenWater) {
                        pastBed = true; // Everything under here is under the sea bed.
                    }
                }
            }
        }
    }

    INFO("searched " << columnsSearched << " columns");
    REQUIRE(waterFound > 0);

    CHECK(aboveSeaLevel == 0);
    CHECK(hangingInAir == 0);
    // Caves under the sea stay dry -- the line where vanilla's aquifers begin and
    // this deliberately does not go.
    CHECK(belowTheBed == 0);
}
