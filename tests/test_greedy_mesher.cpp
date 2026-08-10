#include "mesh/BinaryGreedyMesher.hpp"
#include "mesh/CulledMesher.hpp"
#include "world/BlockRegistry.hpp"
#include "world/Section.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <map>
#include <vector>

using namespace mc;

namespace {

/// Total surface area covered, per face direction.
///
/// This is the equivalence check that matters: merging changes how many quads
/// there are, but must never change which surface is covered. Comparing area
/// against the reference mesher's quad count catches gaps, overlaps and
/// misplaced origins in one assertion.
std::array<usize, kFaceCount> areaPerFace(const ChunkMesh& mesh) {
    std::array<usize, kFaceCount> area{};
    for (const Quad& quad : mesh.quads) {
        area[static_cast<usize>(quad.face())] +=
            static_cast<usize>(quad.width()) * quad.height();
    }
    return area;
}

/// Every unit cell covered by the mesh, as (face, x, y, z) of the cell origin.
std::map<std::array<u32, 4>, u16> coveredCells(const ChunkMesh& mesh) {
    std::map<std::array<u32, 4>, u16> cells;

    // Tangent axes per face, matching kPlans in BinaryGreedyMesher.cpp.
    struct Axes { u32 ux, uy, uz, vx, vy, vz; };
    constexpr std::array<Axes, kFaceCount> kAxes{{
        {0, 0, 1, 0, 1, 0}, // NegX
        {0, 1, 0, 0, 0, 1}, // PosX
        {1, 0, 0, 0, 0, 1}, // NegY
        {0, 0, 1, 1, 0, 0}, // PosY
        {0, 1, 0, 1, 0, 0}, // NegZ
        {1, 0, 0, 0, 1, 0}, // PosZ
    }};

    for (const Quad& quad : mesh.quads) {
        const Axes& axes = kAxes[static_cast<usize>(quad.face())];
        for (u32 dv = 0; dv < quad.height(); ++dv) {
            for (u32 du = 0; du < quad.width(); ++du) {
                const std::array<u32, 4> key{
                    static_cast<u32>(quad.face()),
                    quad.x() + axes.ux * du + axes.vx * dv,
                    quad.y() + axes.uy * du + axes.vy * dv,
                    quad.z() + axes.uz * du + axes.vz * dv,
                };
                cells[key] = quad.material();
            }
        }
    }
    return cells;
}

/// Deterministic pseudo-random terrain, so failures are reproducible.
Section makeTerrainSection() {
    Section section;
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            const auto fx = static_cast<f32>(x);
            const auto fz = static_cast<f32>(z);
            const f32 wave = 10.0f + 5.0f * std::sin(fx * 0.3f) + 4.0f * std::cos(fz * 0.22f);
            const i32 height = static_cast<i32>(wave);
            for (i32 y = 0; y <= height && y < kSectionSize; ++y) {
                BlockId block = kStoneBlock;
                if (y == height) {
                    block = kGrassBlock;
                } else if (y > height - 3) {
                    block = kDirtBlock;
                }
                section.set(x, y, z, block);
            }
        }
    }
    return section;
}

constexpr GreedyMeshOptions kNoAo{.ambientOcclusion = false, .aoAwareMerging = false};

/// A neighbourhood whose 26 neighbours are all the same solid section.
///
/// Replaces the old `emitBoundaryFaces = false` option: suppressing boundary faces
/// used to be a flag standing in for real neighbours, and now that they exist the
/// same situation is expressed by supplying them.
SectionNeighbourhood enclosedBy(const Section& center, const Section& surroundings) {
    SectionNeighbourhood hood;
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dy = -1; dy <= 1; ++dy) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                hood.set(dx, dy, dz, (dx == 0 && dy == 0 && dz == 0) ? &center : &surroundings);
            }
        }
    }
    return hood;
}

} // namespace

TEST_CASE("an empty section produces no quads") {
    Section section;
    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);
    CHECK(mesh.empty());
}

TEST_CASE("a single block emits six unit quads") {
    Section section;
    section.set(10, 10, 10, kStoneBlock);

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    REQUIRE(mesh.quadCount() == 6);
    for (const Quad& quad : mesh.quads) {
        CHECK(quad.width() == 1);
        CHECK(quad.height() == 1);
    }
}

TEST_CASE("a solid section merges each side into one full-size quad") {
    // The strongest possible case for greedy meshing: 6144 unit faces become 6.
    Section section(kStoneBlock);

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    REQUIRE(mesh.quadCount() == kFaceCount);
    for (const Quad& quad : mesh.quads) {
        CHECK(quad.width() == 32);
        CHECK(quad.height() == 32);
    }
}

TEST_CASE("a flat slab merges its top and bottom into single quads") {
    Section section;
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            section.set(x, 0, z, kStoneBlock);
        }
    }

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    const auto area = areaPerFace(mesh);
    CHECK(area[static_cast<usize>(Face::PosY)] == 32 * 32);
    CHECK(area[static_cast<usize>(Face::NegY)] == 32 * 32);
}

TEST_CASE("greedy meshing covers exactly the same surface as the reference mesher") {
    const Section section = makeTerrainSection();

    ChunkMesh reference;
    meshSectionCulled(section, reference);

    ChunkMesh greedy;
    meshSectionGreedy(section, greedy, kNoAo);

    // Merging must reduce the quad count without changing the surface.
    CHECK(greedy.quadCount() < reference.quadCount());

    const auto referenceArea = areaPerFace(reference);
    const auto greedyArea = areaPerFace(greedy);
    for (usize face = 0; face < kFaceCount; ++face) {
        CAPTURE(face);
        REQUIRE(greedyArea[face] == referenceArea[face]);
    }
}

TEST_CASE("greedy meshing covers the same cells, not merely the same area") {
    // Equal area could still hide a quad shifted one cell along with a
    // compensating error elsewhere. This compares the covered cells themselves.
    const Section section = makeTerrainSection();

    ChunkMesh reference;
    meshSectionCulled(section, reference);
    ChunkMesh greedy;
    meshSectionGreedy(section, greedy, kNoAo);

    const auto referenceCells = coveredCells(reference);
    const auto greedyCells = coveredCells(greedy);

    REQUIRE(greedyCells.size() == referenceCells.size());
    for (const auto& [key, material] : referenceCells) {
        const auto found = greedyCells.find(key);
        REQUIRE(found != greedyCells.end());
        // Both meshers put a texture array layer in the material field, so the
        // agreement is on the actual value, not just on which cells are covered.
        REQUIRE(found->second == material);
    }
}

TEST_CASE("no cell is covered twice") {
    const Section section = makeTerrainSection();

    ChunkMesh greedy;
    meshSectionGreedy(section, greedy, kNoAo);

    usize totalArea = 0;
    for (const usize faceArea : areaPerFace(greedy)) {
        totalArea += faceArea;
    }
    CHECK(coveredCells(greedy).size() == totalArea);
}

TEST_CASE("different materials are never merged together") {
    // Two block types side by side along the merge axis.
    Section section;
    for (i32 x = 0; x < kSectionSize; ++x) {
        section.set(x, 0, 0, x < 16 ? kStoneBlock : kSandBlock);
    }

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    for (const Quad& quad : mesh.quads) {
        if (quad.face() == Face::PosY) {
            // Each half merges on its own; neither spans the boundary.
            REQUIRE(quad.width() <= 16);
        }
    }
}

TEST_CASE("a solid section surrounded by solid neighbours is invisible") {
    // The seam case, and the whole reason 3c exists. Meshed in isolation this
    // emits six full-size walls; with neighbours it emits nothing, because every
    // boundary face is hidden by the section next door.
    const Section solid(kStoneBlock);

    ChunkMesh isolated;
    meshSectionGreedy(solid, isolated, kNoAo);
    CHECK(isolated.quadCount() == 6);

    ChunkMesh enclosed;
    meshSectionGreedy(enclosedBy(solid, solid), enclosed, kNoAo);
    CHECK(enclosed.empty());
}

TEST_CASE("only the faces meeting air survive at a boundary") {
    // Solid centre, solid neighbour on +X only: five walls remain, and the +X one
    // is gone. A mistake in which boundary bit is shifted in shows up as the wrong
    // face disappearing.
    const Section solid(kStoneBlock);

    SectionNeighbourhood hood;
    hood.set(0, 0, 0, &solid);
    hood.set(1, 0, 0, &solid);

    ChunkMesh mesh;
    meshSectionGreedy(hood, mesh, kNoAo);

    CHECK(mesh.quadCount() == 5);
    for (const Quad& quad : mesh.quads) {
        CHECK(quad.face() != Face::PosX);
    }

    // And the mirror image, so a sign error cannot pass both.
    SectionNeighbourhood below;
    below.set(0, 0, 0, &solid);
    below.set(0, -1, 0, &solid);

    ChunkMesh other;
    meshSectionGreedy(below, other, kNoAo);
    CHECK(other.quadCount() == 5);
    for (const Quad& quad : other.quads) {
        CHECK(quad.face() != Face::NegY);
    }
}

TEST_CASE("a partly solid neighbour hides exactly the faces it covers") {
    Section center(kStoneBlock);

    // A neighbour on +X that is solid only in the lower half of its x = 0 plane.
    Section neighbour;
    for (i32 y = 0; y < 16; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            neighbour.set(0, y, z, kStoneBlock);
        }
    }

    SectionNeighbourhood hood;
    hood.set(0, 0, 0, &center);
    hood.set(1, 0, 0, &neighbour);

    ChunkMesh mesh;
    meshSectionGreedy(hood, mesh, kNoAo);

    usize posXArea = 0;
    for (const Quad& quad : mesh.quads) {
        if (quad.face() == Face::PosX) {
            posXArea += static_cast<usize>(quad.width()) * quad.height();
        }
    }
    // Half of the 32x32 plane is covered, so half of it still faces air.
    CHECK(posXArea == 16 * 32);
}

TEST_CASE("greedy and reference meshers agree across a section boundary") {
    // The equivalence check that matters for 3c: both meshers have to read
    // neighbours the same way, or the oracle stops being one.
    Section center;
    Section neighbour;
    for (i32 y = 0; y < kSectionSize; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                // A deterministic scatter, so the boundary planes are ragged.
                if (((x * 7 + y * 13 + z * 23) % 5) < 3) {
                    center.set(x, y, z, kStoneBlock);
                }
                if (((x * 11 + y * 5 + z * 17) % 4) < 2) {
                    neighbour.set(x, y, z, kDirtBlock);
                }
            }
        }
    }

    SectionNeighbourhood hood;
    hood.set(0, 0, 0, &center);
    hood.set(1, 0, 0, &neighbour);
    hood.set(-1, 0, 0, &neighbour);
    hood.set(0, 1, 0, &neighbour);
    hood.set(0, 0, -1, &neighbour);

    ChunkMesh reference;
    meshSectionCulled(hood, reference);
    ChunkMesh greedy;
    meshSectionGreedy(hood, greedy, kNoAo);

    const auto referenceArea = areaPerFace(reference);
    const auto greedyArea = areaPerFace(greedy);
    for (usize face = 0; face < kFaceCount; ++face) {
        CAPTURE(face);
        REQUIRE(greedyArea[face] == referenceArea[face]);
    }
    CHECK(coveredCells(greedy) == coveredCells(reference));
    CHECK(greedy.quadCount() < reference.quadCount());
}

TEST_CASE("AO is occluded by blocks in the neighbouring section") {
    // The half of neighbour awareness that boundary culling alone would miss.
    // A single block on the section's top face, with a neighbour above that is
    // solid: its top face is hidden, but its side faces must be darkened by the
    // blocks overhead rather than lit as if the sky were open.
    Section center;
    center.set(5, kSectionSize - 1, 5, kStoneBlock);

    Section above(kStoneBlock);

    SectionNeighbourhood open;
    open.set(0, 0, 0, &center);

    SectionNeighbourhood covered;
    covered.set(0, 0, 0, &center);
    covered.set(0, 1, 0, &above);

    ChunkMesh openMesh;
    meshSectionGreedy(open, openMesh);
    ChunkMesh coveredMesh;
    meshSectionGreedy(covered, coveredMesh);

    const auto sideAoTotal = [](const ChunkMesh& mesh) {
        u32 total = 0;
        for (const Quad& quad : mesh.quads) {
            if (quad.face() == Face::PosY || quad.face() == Face::NegY) {
                continue;
            }
            const u8 ao = quad.ao();
            for (u32 corner = 0; corner < 4; ++corner) {
                total += (ao >> (corner * 2)) & 0x3u;
            }
        }
        return total;
    };

    // Strictly darker, and the top face is gone.
    CHECK(sideAoTotal(coveredMesh) < sideAoTotal(openMesh));
    for (const Quad& quad : coveredMesh.quads) {
        CHECK(quad.face() != Face::PosY);
    }
}

TEST_CASE("a wall in the next section darkens the floor at the seam") {
    // Constructed so the answer is unambiguous. A flat floor filling the centre
    // section's y = 0 layer has every top face fully open, so the whole plane
    // merges into a single quad with AO 0xFF. Put a wall in the +X neighbour, at
    // its x = 0 and y = 1, and only the floor faces at x = 31 change: that wall is
    // the V-side neighbour of their corners. If neighbour AO is not read, nothing
    // changes at all.
    //
    // For PosY the tangent basis is U = z, V = x (see kPlans), so a quad's V extent
    // is `height` and its V origin is `x` -- getting that backwards is what makes a
    // test like this pass for the wrong reason.
    Section floor;
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            floor.set(x, 0, z, kStoneBlock);
        }
    }

    Section wall;
    for (i32 z = 0; z < kSectionSize; ++z) {
        wall.set(0, 1, z, kStoneBlock);
    }

    const auto topQuads = [](const ChunkMesh& mesh) {
        std::vector<Quad> quads;
        for (const Quad& quad : mesh.quads) {
            if (quad.face() == Face::PosY) {
                quads.push_back(quad);
            }
        }
        return quads;
    };

    SectionNeighbourhood open;
    open.set(0, 0, 0, &floor);

    ChunkMesh openMesh;
    meshSectionGreedy(open, openMesh);
    const auto openTops = topQuads(openMesh);

    REQUIRE(openTops.size() == 1);
    CHECK(openTops[0].width() == 32);  // z
    CHECK(openTops[0].height() == 32); // x
    CHECK(openTops[0].ao() == 0xFF);   // Every corner fully open.

    SectionNeighbourhood beside;
    beside.set(0, 0, 0, &floor);
    beside.set(1, 0, 0, &wall);

    ChunkMesh besideMesh;
    meshSectionGreedy(beside, besideMesh);
    const auto besideTops = topQuads(besideMesh);

    // The uniform plane has to split, because one row of it is now darker.
    REQUIRE(besideTops.size() >= 2);

    bool foundSeamRow = false;
    for (const Quad& quad : besideTops) {
        const bool touchesSeam = quad.x() + quad.height() == kSectionSize;
        if (touchesSeam) {
            foundSeamRow = true;
            CHECK(quad.ao() != 0xFF); // Darkened by the neighbour's wall.
            CHECK(quad.height() == 1); // Only the x = 31 row is affected.
        } else {
            CHECK(quad.ao() == 0xFF); // Everything away from the seam is untouched.
        }
    }
    CHECK(foundSeamRow);

    // Total covered area is unchanged: merging split, the surface did not.
    CHECK(areaPerFace(besideMesh)[static_cast<usize>(Face::PosY)]
          == areaPerFace(openMesh)[static_cast<usize>(Face::PosY)]);
}

TEST_CASE("AO-aware merging never covers less surface than AO-ignoring merging") {
    const Section section = makeTerrainSection();

    ChunkMesh withAo;
    meshSectionGreedy(section, withAo,
                      GreedyMeshOptions{.ambientOcclusion = true, .aoAwareMerging = true});
    ChunkMesh withoutAo;
    meshSectionGreedy(section, withoutAo, kNoAo);

    // Requiring AO to match can only split quads further, never merge more.
    CHECK(withAo.quadCount() >= withoutAo.quadCount());

    const auto aoArea = areaPerFace(withAo);
    const auto plainArea = areaPerFace(withoutAo);
    for (usize face = 0; face < kFaceCount; ++face) {
        CAPTURE(face);
        REQUIRE(aoArea[face] == plainArea[face]);
    }
}

TEST_CASE("a fully enclosed cavity is meshed from the inside") {
    Section section(kStoneBlock);
    for (i32 y = 10; y < 14; ++y) {
        for (i32 z = 10; z < 14; ++z) {
            for (i32 x = 10; x < 14; ++x) {
                section.set(x, y, z, kAirBlock);
            }
        }
    }

    const Section solid(kStoneBlock);
    const SectionNeighbourhood hood = enclosedBy(section, solid);

    ChunkMesh greedy;
    meshSectionGreedy(hood, greedy, kNoAo);

    ChunkMesh reference;
    meshSectionCulled(hood, reference);

    // Six inward-facing 4x4 walls.
    const auto area = areaPerFace(greedy);
    for (usize face = 0; face < kFaceCount; ++face) {
        CAPTURE(face);
        REQUIRE(area[face] == 16);
    }
    CHECK(greedy.quadCount() == 6);
    CHECK(reference.quadCount() == 6 * 16);
}

TEST_CASE("texture layers follow the face, not just the block") {
    Section section;
    section.set(5, 5, 5, kGrassBlock);

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh, kNoAo);

    const BlockRegistry& registry = BlockRegistry::instance();
    for (const Quad& quad : mesh.quads) {
        REQUIRE(quad.material() == registry.textureLayer(kGrassBlock, quad.face()));
    }

    // Grass really does use three different layers.
    CHECK(registry.textureLayer(kGrassBlock, Face::PosY)
          != registry.textureLayer(kGrassBlock, Face::NegX));
    CHECK(registry.textureLayer(kGrassBlock, Face::NegY)
          != registry.textureLayer(kGrassBlock, Face::PosY));
}
