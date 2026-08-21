#include "world/BlockLight.hpp"
#include "world/BlockTable.hpp"
#include "world/SkyLight.hpp"
#include "world/World.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace mc;

namespace {

/// A world whose columns are all Ready, roofed at y = 64 and dark underneath.
///
/// **Roofed on purpose, and it is the whole point of the fixture.** Under open sky
/// every cell is already at 15 and the mesher's `max(sky, block)` would hide a torch
/// completely -- which is correct behaviour and exactly why a test that wants to see
/// block light has to get out of the daylight first. The interesting place for a
/// torch is indoors, so the fixture builds indoors.
///
/// Render distance 1 gives a 3x3 of columns, which is what a cross-column test needs.
std::unique_ptr<World> darkWorld(i32 renderDistance = 1) {
    auto world = std::make_unique<World>(renderDistance);
    world->updateLoadedRegion(ChunkPos{0, 0});
    world->forEachChunk([](Chunk& chunk) {
        chunk.setState(ChunkState::Ready);

        Section* roof = chunk.sectionAt(blockToSectionCoord(64));
        REQUIRE(roof != nullptr);
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                roof->set(x, blockToLocalCoord(64), z, kStoneBlock);
            }
        }
        computeSkyLight(chunk);
    });
    return world;
}

u8 blockLightAt(const World& world, BlockPos pos) {
    const Chunk* chunk = world.find(toChunkPos(pos));
    REQUIRE(chunk != nullptr);
    const Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
    REQUIRE(section != nullptr);
    return section->blockLight(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                               blockToLocalCoord(pos.z));
}

u8 skyLightAt(const World& world, BlockPos pos) {
    const Chunk* chunk = world.find(toChunkPos(pos));
    REQUIRE(chunk != nullptr);
    const Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
    REQUIRE(section != nullptr);
    return section->skyLight(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                             blockToLocalCoord(pos.z));
}

/// What the mesher would draw with. The combination lives in `Section::light`.
u8 drawnLightAt(const World& world, BlockPos pos) {
    const Chunk* chunk = world.find(toChunkPos(pos));
    REQUIRE(chunk != nullptr);
    const Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
    REQUIRE(section != nullptr);
    return section->light(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                          blockToLocalCoord(pos.z));
}

/// A spot well inside the roofed region and well away from any column wall.
constexpr BlockPos kIndoors{5, 40, 5};

} // namespace

TEST_CASE("a torch lights its own cell at vanilla's level") {
    auto world = darkWorld();
    REQUIRE(blockLightAt(*world, kIndoors) == 0);

    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);
    CHECK(blockLightAt(*world, kIndoors) == 14);
}

TEST_CASE("block light falls off by one per block") {
    auto world = darkWorld();
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);

    // Straight out along one axis: 14 at the source, 13 next to it, and so on down to
    // nothing at fifteen blocks. Vanilla's falloff exactly.
    for (i32 step = 1; step <= 15; ++step) {
        const BlockPos at{kIndoors.x + step, kIndoors.y, kIndoors.z};
        CAPTURE(step);
        CHECK(blockLightAt(*world, at) == (step < 14 ? 14 - step : 0));
    }
}

TEST_CASE("light spreads diagonally at the taxicab distance") {
    auto world = darkWorld();
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);

    // Light walks the grid rather than flying, so a cell three across and two up is
    // five steps away and five levels down. This is what makes a lit room fall off
    // into its corners rather than in a sphere.
    const BlockPos corner{kIndoors.x + 3, kIndoors.y + 2, kIndoors.z};
    CHECK(blockLightAt(*world, corner) == 14 - 5);
}

TEST_CASE("a torch lights across a column boundary with no seam") {
    // **The test this whole design exists for.** A column is 32 blocks wide and a
    // torch reaches 15, so light stops at a column wall unless it is allowed to
    // cross. Sky light is not allowed to and documents the seam as acceptable,
    // because its vertical fill is exact and only cave interiors near a border come
    // out dim. Block light has no such excuse: a torch has to sit in a 3x3 patch at
    // the very centre of a column for its light to stay inside one, which is nine
    // positions out of a thousand and twenty-four.
    auto world = darkWorld();

    // One block in from the wall between column 0 and column 1.
    const BlockPos nearWall{kSectionSize - 1, 40, 5};
    REQUIRE(toChunkPos(nearWall) == ChunkPos{0, 0});
    REQUIRE(world->setBlock(nearWall, kTorchBlock) == World::EditStatus::Applied);

    // The very next cell is in the next column along, and it must be one level down
    // rather than dark.
    const BlockPos overTheWall{kSectionSize, 40, 5};
    REQUIRE(toChunkPos(overTheWall) == ChunkPos{1, 0});
    CHECK(blockLightAt(*world, overTheWall) == 13);

    // And the falloff continues into that column as if the wall were not there.
    CHECK(blockLightAt(*world, BlockPos{kSectionSize + 4, 40, 5}) == 9);
    CHECK(blockLightAt(*world, BlockPos{kSectionSize + 12, 40, 5}) == 1);
}

TEST_CASE("breaking a torch takes its light back") {
    auto world = darkWorld();
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);
    REQUIRE(blockLightAt(*world, BlockPos{kIndoors.x + 4, kIndoors.y, kIndoors.z}) == 10);

    REQUIRE(world->setBlock(kIndoors, kAirBlock) == World::EditStatus::Applied);

    CHECK(blockLightAt(*world, kIndoors) == 0);
    for (i32 step = 1; step <= 15; ++step) {
        CAPTURE(step);
        CHECK(blockLightAt(*world, BlockPos{kIndoors.x + step, kIndoors.y, kIndoors.z})
              == 0);
    }
}

TEST_CASE("breaking one torch leaves the light of another") {
    // **The case a naive removal gets wrong**, and the reason the removal pass sorts
    // its neighbours by whether they are dimmer than the cell going dark. A cell as
    // bright or brighter has its own path to some other source, so it survives and
    // re-spreads instead of being cleared.
    auto world = darkWorld();

    const BlockPos first = kIndoors;
    const BlockPos second{kIndoors.x + 6, kIndoors.y, kIndoors.z};
    REQUIRE(world->setBlock(first, kTorchBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(second, kTorchBlock) == World::EditStatus::Applied);

    // Midway between them, lit to 11 by both.
    const BlockPos between{kIndoors.x + 3, kIndoors.y, kIndoors.z};
    REQUIRE(blockLightAt(*world, between) == 11);

    REQUIRE(world->setBlock(first, kAirBlock) == World::EditStatus::Applied);

    // The survivor still lights the whole span, at its own distances.
    CHECK(blockLightAt(*world, second) == 14);
    CHECK(blockLightAt(*world, between) == 11);
    CHECK(blockLightAt(*world, first) == 14 - 6);
}

TEST_CASE("an opaque block casts a shadow") {
    auto world = darkWorld();
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);

    const BlockPos wall{kIndoors.x + 1, kIndoors.y, kIndoors.z};
    const BlockPos behind{kIndoors.x + 2, kIndoors.y, kIndoors.z};
    REQUIRE(blockLightAt(*world, behind) == 12);

    REQUIRE(world->setBlock(wall, kStoneBlock) == World::EditStatus::Applied);

    // The cell the wall fills holds nothing, and what was straight behind it is now
    // lit the long way round -- over the top and down, which is four steps rather
    // than two.
    CHECK(blockLightAt(*world, wall) == 0);
    CHECK(blockLightAt(*world, behind) == 10);
}

TEST_CASE("opening a wall lets the light through") {
    auto world = darkWorld();

    const BlockPos wall{kIndoors.x + 1, kIndoors.y, kIndoors.z};
    REQUIRE(world->setBlock(wall, kStoneBlock) == World::EditStatus::Applied);
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);
    REQUIRE(blockLightAt(*world, BlockPos{kIndoors.x + 2, kIndoors.y, kIndoors.z}) == 10);

    REQUIRE(world->setBlock(wall, kAirBlock) == World::EditStatus::Applied);

    // Straight through now, so the cell past it goes from the long way round to the
    // short one.
    CHECK(blockLightAt(*world, wall) == 13);
    CHECK(blockLightAt(*world, BlockPos{kIndoors.x + 2, kIndoors.y, kIndoors.z}) == 12);
}

TEST_CASE("sky and block light are kept apart and combined with max") {
    // DESIGN.md 3.7. The two channels are stored separately and only the mesher sees
    // one number, because breaking a torch has to know how much of the brightness
    // under it was daylight in order to know what to leave behind.
    auto world = darkWorld();

    // Above the roof, in full daylight.
    const BlockPos outdoors{5, 70, 5};
    REQUIRE(skyLightAt(*world, outdoors) == 15);
    REQUIRE(blockLightAt(*world, outdoors) == 0);
    CHECK(drawnLightAt(*world, outdoors) == 15);

    // A torch out here changes what is drawn by nothing at all, because daylight
    // already wins -- and the block channel still records it.
    const BlockPos torch{7, 70, 5};
    REQUIRE(world->setBlock(torch, kTorchBlock) == World::EditStatus::Applied);
    CHECK(blockLightAt(*world, outdoors) == 12);
    CHECK(skyLightAt(*world, outdoors) == 15);
    CHECK(drawnLightAt(*world, outdoors) == 15);

    // Indoors the block channel is the only one there is, so it is what gets drawn.
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);
    CHECK(skyLightAt(*world, kIndoors) == 0);
    CHECK(drawnLightAt(*world, kIndoors) == 14);
}

TEST_CASE("an edit with no light in reach moves nothing") {
    // The predicate that keeps the pin check and the flood off the digging path. In a
    // world nobody has put a torch in, this is every edit there is.
    auto world = darkWorld();

    CHECK_FALSE(blockLightCanMove(*world, kIndoors, kAirBlock, kStoneBlock));
    CHECK_FALSE(blockLightCanMove(*world, kIndoors, kStoneBlock, kDirtBlock));

    // Placing a torch always moves light, and so does editing next to one.
    CHECK(blockLightCanMove(*world, kIndoors, kAirBlock, kTorchBlock));

    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);
    const BlockPos beside{kIndoors.x + 1, kIndoors.y, kIndoors.z};
    CHECK(blockLightCanMove(*world, beside, kAirBlock, kStoneBlock));
}

TEST_CASE("seeding relights a column that arrived with a torch in it") {
    // **Block light is derived, so it is not saved.** A column comes back from disk
    // holding its torches and no light at all, exactly as a freshly generated one
    // does. This is what puts the light back.
    auto world = darkWorld();

    // Written straight into the section, which is what loading from disk does -- it
    // does not go through `setBlock`, so nothing has spread any light.
    Chunk* chunk = world->find(ChunkPos{0, 0});
    REQUIRE(chunk != nullptr);
    Section* section = chunk->sectionAt(blockToSectionCoord(kIndoors.y));
    REQUIRE(section != nullptr);
    section->set(blockToLocalCoord(kIndoors.x), blockToLocalCoord(kIndoors.y),
                 blockToLocalCoord(kIndoors.z), kTorchBlock);
    REQUIRE(blockLightAt(*world, kIndoors) == 0);

    LightTouch touched;

    // **Seeding finds nothing until the column says it holds an emitter**, which is
    // the gate that keeps this whole pass off the streaming path -- nine flag reads
    // per column that arrives, instead of a hundred and eight palette scans. The
    // worker that fills a column is what sets it, so a test writing voxels straight
    // in has to do the same thing in the same order.
    seedBlockLight(*world, ChunkPos{0, 0}, touched);
    REQUIRE(blockLightAt(*world, kIndoors) == 0);
    REQUIRE(touched.empty());

    noteEmitters(*chunk);
    CHECK(chunk->hasEmitter());

    seedBlockLight(*world, ChunkPos{0, 0}, touched);

    CHECK(blockLightAt(*world, kIndoors) == 14);
    CHECK(blockLightAt(*world, BlockPos{kIndoors.x + 4, kIndoors.y, kIndoors.z}) == 10);
    CHECK_FALSE(touched.empty());
}

TEST_CASE("seeding is idempotent") {
    // Called on every column that becomes ready, without tracking which of its
    // neighbours have already been done -- so running it twice has to be the same as
    // running it once. It is, because the add pass only ever raises a cell.
    auto world = darkWorld();
    REQUIRE(world->setBlock(kIndoors, kTorchBlock) == World::EditStatus::Applied);

    const u8 before = blockLightAt(*world, BlockPos{kIndoors.x + 4, kIndoors.y, kIndoors.z});

    LightTouch touched;
    seedBlockLight(*world, ChunkPos{0, 0}, touched);
    seedBlockLight(*world, ChunkPos{0, 0}, touched);

    CHECK(blockLightAt(*world, kIndoors) == 14);
    CHECK(blockLightAt(*world, BlockPos{kIndoors.x + 4, kIndoors.y, kIndoors.z}) == before);
    // Nothing moved, so nothing needs remeshing.
    CHECK(touched.empty());
}

TEST_CASE("seeding reaches a torch standing in a neighbouring column") {
    // A torch one block inside a border owes light to the column next door, and
    // neither of the two can know which loaded first. Seeding from the neighbours as
    // well as from the column itself is what covers both directions.
    auto world = darkWorld();

    Chunk* chunk = world->find(ChunkPos{0, 0});
    REQUIRE(chunk != nullptr);
    Section* section = chunk->sectionAt(blockToSectionCoord(40));
    REQUIRE(section != nullptr);
    section->set(kSectionSize - 1, blockToLocalCoord(40), 5, kTorchBlock);
    noteEmitters(*chunk);

    // Seeded from column 1, which does not contain the torch at all -- so the flag
    // that lets the flood happen is read off the *neighbour*, which is the whole
    // reason the gate looks at nine columns rather than one.
    LightTouch touched;
    seedBlockLight(*world, ChunkPos{1, 0}, touched);

    CHECK(blockLightAt(*world, BlockPos{kSectionSize, 40, 5}) == 13);
}

TEST_CASE("moving light dirties the sections that have to be remeshed") {
    // A section's mesh is built over a padded grid that reaches one voxel into every
    // neighbour, light included, so a cell that changed on a section wall is read by
    // the sections on the other side of it.
    auto world = darkWorld();
    world->forEachChunk([](Chunk& chunk) {
        for (usize i = 0; i < Chunk::kSectionCount; ++i) {
            chunk.clearSectionDirty(i);
        }
    });

    // On the wall between column 0 and column 1, so the light crosses it.
    const BlockPos nearWall{kSectionSize - 1, 40, 5};
    REQUIRE(world->setBlock(nearWall, kTorchBlock) == World::EditStatus::Applied);

    const auto sectionIndex =
        static_cast<usize>(sectionIndexInColumn(blockToSectionCoord(40)));

    const Chunk* here = world->find(ChunkPos{0, 0});
    const Chunk* next = world->find(ChunkPos{1, 0});
    REQUIRE(here != nullptr);
    REQUIRE(next != nullptr);

    CHECK(here->isSectionDirty(sectionIndex));
    // The column the light spilled into has to be remeshed too, and this is the thing
    // a per-column dirty mask could not have said.
    CHECK(next->isSectionDirty(sectionIndex));
}
