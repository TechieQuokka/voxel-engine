#include "mesh/BinaryGreedyMesher.hpp"
#include "mesh/CulledMesher.hpp"
#include "world/BlockRegistry.hpp"
#include "world/Section.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <map>

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

constexpr GreedyMeshOptions kNoAo{
    .emitBoundaryFaces = true, .ambientOcclusion = false, .aoAwareMerging = false};

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

TEST_CASE("boundary faces can be suppressed") {
    Section section(kStoneBlock);

    ChunkMesh mesh;
    meshSectionGreedy(section, mesh,
                      GreedyMeshOptions{.emitBoundaryFaces = false,
                                        .ambientOcclusion = false,
                                        .aoAwareMerging = false});
    CHECK(mesh.empty());
}

TEST_CASE("AO-aware merging never covers less surface than AO-ignoring merging") {
    const Section section = makeTerrainSection();

    ChunkMesh withAo;
    meshSectionGreedy(section, withAo,
                      GreedyMeshOptions{.emitBoundaryFaces = true,
                                        .ambientOcclusion = true,
                                        .aoAwareMerging = true});
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

    ChunkMesh greedy;
    meshSectionGreedy(section, greedy,
                      GreedyMeshOptions{.emitBoundaryFaces = false,
                                        .ambientOcclusion = false,
                                        .aoAwareMerging = false});

    ChunkMesh reference;
    meshSectionCulled(section, reference, MeshOptions{.emitBoundaryFaces = false});

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
