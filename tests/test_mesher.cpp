#include "mesh/CulledMesher.hpp"
#include "world/BlockRegistry.hpp"
#include "world/Section.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace mc;

namespace {

usize countFaces(const ChunkMesh& mesh, Face face) {
    return static_cast<usize>(
        std::count_if(mesh.quads.begin(), mesh.quads.end(),
                      [face](const Quad& quad) { return quad.face() == face; }));
}

} // namespace

TEST_CASE("Quad round-trips every packed field") {
    const Quad quad = Quad::make(32, 17, 5, 40, 64, Face::PosY, 4321, 0xB7);

    CHECK(quad.x() == 32);
    CHECK(quad.y() == 17);
    CHECK(quad.z() == 5);
    CHECK(quad.width() == 40);
    CHECK(quad.height() == 64);
    CHECK(quad.face() == Face::PosY);
    CHECK(quad.material() == 4321);
    CHECK(quad.ao() == 0xB7);
}

TEST_CASE("Quad encodes coordinate 32, the far face plane") {
    // A face on the positive boundary of a 32^3 section sits at 32, which is
    // why the position fields are 6 bits rather than 5.
    const Quad quad = Quad::make(32, 32, 32, 1, 1, Face::PosX, 1);
    CHECK(quad.x() == 32);
    CHECK(quad.y() == 32);
    CHECK(quad.z() == 32);
}

TEST_CASE("every face value survives packing") {
    for (u32 i = 0; i < kFaceCount; ++i) {
        const auto face = static_cast<Face>(i);
        const Quad quad = Quad::make(1, 2, 3, 1, 1, face, 0);
        REQUIRE(quad.face() == face);
    }
}

TEST_CASE("an empty section produces no quads") {
    Section section;
    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    CHECK(mesh.empty());
}

TEST_CASE("a single block emits exactly six faces, one per direction") {
    Section section;
    section.set(10, 10, 10, kStoneBlock);

    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    REQUIRE(mesh.quadCount() == 6);
    for (u32 i = 0; i < kFaceCount; ++i) {
        REQUIRE(countFaces(mesh, static_cast<Face>(i)) == 1);
    }
}

TEST_CASE("touching blocks hide the faces between them") {
    Section section;
    section.set(10, 10, 10, kStoneBlock);
    section.set(11, 10, 10, kStoneBlock);

    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    // 12 faces total, minus the two that face each other.
    CHECK(mesh.quadCount() == 10);
    CHECK(countFaces(mesh, Face::PosX) == 1);
    CHECK(countFaces(mesh, Face::NegX) == 1);
}

TEST_CASE("a fully solid section emits only its outer shell") {
    Section section(kStoneBlock);
    REQUIRE(section.isUniform());

    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    // Six faces of 32x32 with 1x1 quads; interior faces are all hidden.
    constexpr usize kExpected = 6 * 32 * 32;
    CHECK(mesh.quadCount() == kExpected);
}

TEST_CASE("boundary faces can be suppressed") {
    Section section(kStoneBlock);

    ChunkMesh mesh;
    meshSectionCulled(section, mesh, MeshOptions{.emitBoundaryFaces = false});

    // With neighbours assumed solid, a fully solid section is invisible.
    CHECK(mesh.empty());
}

TEST_CASE("quad origins sit on the correct side of the voxel") {
    Section section;
    section.set(4, 5, 6, kStoneBlock);

    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    for (const Quad& quad : mesh.quads) {
        switch (quad.face()) {
        case Face::NegX: CHECK(quad.x() == 4); break;
        case Face::PosX: CHECK(quad.x() == 5); break;   // far plane of the cell
        case Face::NegY: CHECK(quad.y() == 5); break;
        case Face::PosY: CHECK(quad.y() == 6); break;
        case Face::NegZ: CHECK(quad.z() == 6); break;
        case Face::PosZ: CHECK(quad.z() == 7); break;
        }
    }
}

TEST_CASE("non-opaque blocks do not emit faces and do not hide neighbours") {
    Section section;
    section.set(10, 10, 10, kStoneBlock);
    section.set(11, 10, 10, kAirBlock);

    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    CHECK(mesh.quadCount() == 6);
}

TEST_CASE("material carries the texture layer for the face") {
    Section section;
    section.set(1, 1, 1, kGrassBlock);

    ChunkMesh mesh;
    meshSectionCulled(section, mesh);

    // Same convention as the greedy mesher: the field is a texture array layer,
    // so a mesh from either one renders identically.
    const BlockRegistry& registry = BlockRegistry::instance();
    REQUIRE_FALSE(mesh.empty());
    for (const Quad& quad : mesh.quads) {
        REQUIRE(quad.material() == registry.textureLayer(kGrassBlock, quad.face()));
    }
}
