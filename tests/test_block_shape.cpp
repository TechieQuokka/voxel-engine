#include "world/BlockShape.hpp"
#include "world/WalkMove.hpp"
#include "world/BlockRegistry.hpp"
#include "world/ItemTable.hpp"
#include "world/PlayerBox.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

/// Standing in the middle of the block at (8, 64, 8), the same case
/// test_player_box.cpp builds every assertion from.
PlayerBox standing() {
    return PlayerBox{vec3{8.5f, 64.0f, 8.5f}};
}

/// The overload the collision path actually calls, spelled once here so a test reads
/// as "does the player hit this shape in this cell".
bool hits(const PlayerBox& box, BlockPos pos, const BlockBox& shape) {
    return box.intersectsBox(pos, shape.lowX(), shape.lowY(), shape.lowZ(),
                             shape.highX(), shape.highY(), shape.highZ());
}

} // namespace

TEST_CASE("a full cube box collides exactly as the unit-cube test did") {
    // **This is the no-regression case and it is the important one.** Every block in
    // the world is a cube, so if the box path disagrees with `intersects` anywhere,
    // the split has changed how the whole world collides.
    const PlayerBox box = standing();
    const BlockBox cube{0, 0, 0, 16, 16, 16};

    for (i32 y = 62; y <= 67; ++y) {
        for (i32 z = 7; z <= 9; ++z) {
            for (i32 x = 7; x <= 9; ++x) {
                const BlockPos pos{x, y, z};
                CHECK(hits(box, pos, cube) == box.intersects(pos));
            }
        }
    }
}

TEST_CASE("every solid block in the table is a cube, and reports one full box") {
    // The state this phase starts from. When a slab lands this test keeps its meaning
    // by naming the shapes that are allowed to be otherwise.
    for (BlockId id = 0; id < kBlocks.size(); ++id) {
        const std::span<const BlockBox> boxes = blockBoxes(id);
        if (!isSolidBlock(id)) {
            CHECK(boxes.empty());
            continue;
        }
        if (shapeOf(id) != BlockShape::Cube) {
            continue;
        }
        REQUIRE(boxes.size() == 1);
        CHECK(boxes[0] == BlockBox{0, 0, 0, 16, 16, 16});
    }
}

TEST_CASE("air and water hold nothing up") {
    CHECK(blockBoxes(blockIdOf("air")).empty());
    CHECK(blockBoxes(blockIdOf("water")).empty());
}

TEST_CASE("a bottom slab is half a block, and its top is where you stand") {
    const std::span<const BlockBox> boxes = detail::kSlabBottomBoxes;
    REQUIRE(boxes.size() == 1);

    // Eight sixteenths, which is 0.5 exactly -- not 0.5f accumulated from anywhere.
    CHECK(boxes[0].maxY == 8);
    CHECK(boxes[0].topFraction() == doctest::Approx(0.5f));

    // And it fills its cell horizontally, which is what makes a slab floor walkable
    // rather than something you fall between.
    CHECK(boxes[0].minX == 0);
    CHECK(boxes[0].maxX == 16);
    CHECK(boxes[0].minZ == 0);
    CHECK(boxes[0].maxZ == 16);
}

TEST_CASE("standing on a bottom slab leaves the upper half of the cell free") {
    // Feet at the slab's top surface: y = 64.5. The slab is in cell y = 64, and the
    // player's own feet are level with its top, so by the touching-is-not-overlapping
    // rule the slab must read as clear.
    const PlayerBox box{vec3{8.5f, 64.5f, 8.5f}};
    CHECK_FALSE(hits(box, BlockPos{8, 64, 8}, detail::kSlabBottomBoxes[0]));

    // A *cube* in that same cell would not be clear, which is the difference the
    // shape makes.
    CHECK(hits(box, BlockPos{8, 64, 8}, BlockBox{0, 0, 0, 16, 16, 16}));
}

TEST_CASE("a bottom slab is walked into when the feet are on the cell floor") {
    // Standing at y = 64 with a slab in cell 64: the slab's upper surface is half a
    // block above the feet, so it blocks. This is the case step-up has to resolve.
    const PlayerBox box = standing();
    CHECK(hits(box, BlockPos{8, 64, 8}, detail::kSlabBottomBoxes[0]));
}

TEST_CASE("a top slab blocks the upper half and leaves the lower half free") {
    const BlockBox top = detail::kSlabTopBoxes[0];
    CHECK(top.minY == 8);
    CHECK(top.maxY == 16);

    // Feet on the cell floor: the top slab starts half a block up, so it is hit.
    CHECK(hits(standing(), BlockPos{8, 64, 8}, top));

    // A whole block below is untouched by it.
    CHECK_FALSE(hits(standing(), BlockPos{8, 63, 8}, top));
}

TEST_CASE("half a block is under vanilla's step height and a whole one is not") {
    // **The reason `kStepHeight` is 0.6 and the reason a slab is worth having.** The
    // comment in WalkMove.hpp says 0.6 covers slabs and stairs and nothing else; with
    // every block a full cube that constant has never once been able to do its job.
    CHECK(detail::kSlabBottomBoxes[0].topFraction() < WalkMove::kStepHeight);
    CHECK(detail::kCubeBoxes[0].topFraction() > WalkMove::kStepHeight);
}

TEST_CASE("a slab clicked onto a floor rests on it, and onto a ceiling hangs from it") {
    // Vanilla's rule, and the one a player never thinks about because it is the only
    // one that is not surprising: a slab put down on the ground is on the ground.
    CHECK_FALSE(slabGoesInTopHalf(Face::PosY, 64.0f));
    CHECK(slabGoesInTopHalf(Face::NegY, 64.0f));
}

TEST_CASE("a slab clicked on a side takes the half of the face it was clicked on") {
    for (const Face face : {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ}) {
        CHECK_FALSE(slabGoesInTopHalf(face, 64.1f));  // lower half
        CHECK_FALSE(slabGoesInTopHalf(face, 64.49f));
        CHECK(slabGoesInTopHalf(face, 64.5f));        // the boundary belongs to the top
        CHECK(slabGoesInTopHalf(face, 64.9f));
    }
}

TEST_CASE("the half is right below y = 0, where truncation rounds the wrong way") {
    // **A cast truncates toward zero.** At y = -3.25 the fraction is 0.75 and the
    // slab belongs in the top half; a plain cast would compute -0.25 and answer the
    // opposite. Half the world is below zero, so this is not a corner case.
    CHECK(slabGoesInTopHalf(Face::PosX, -3.25f));
    CHECK_FALSE(slabGoesInTopHalf(Face::PosX, -3.75f));

    // Exactly on a negative boundary is a fraction of zero, not of one.
    CHECK_FALSE(slabGoesInTopHalf(Face::PosX, -3.0f));
}

TEST_CASE("placedVariant leaves everything that is not a slab alone") {
    const BlockId stone = blockIdOf("stone");
    CHECK(placedVariant(stone, Face::NegY, 64.9f) == stone);
    CHECK(placedVariant(stone, Face::PosY, 64.0f) == stone);
}

TEST_CASE("placedVariant flips a slab to its top half and back") {
    const BlockId slab = blockIdOf("oak_slab");
    const BlockId top = blockIdOf("oak_slab_top");

    CHECK(placedVariant(slab, Face::PosY, 64.0f) == slab);
    CHECK(placedVariant(slab, Face::NegY, 64.0f) == top);
    CHECK(placedVariant(slab, Face::PosX, 64.8f) == top);
    CHECK(placedVariant(slab, Face::PosX, 64.2f) == slab);
}

TEST_CASE("both slab halves shade light but hide no neighbouring face") {
    // The pair the old single `opaque` flag could not express, and the reason it was
    // split. Getting either of these backwards is visible immediately: `opaque` true
    // makes a slab against a wall draw as a solid wall, and `blocksLight` false makes
    // a slab roof let daylight through.
    for (const char* name : {"oak_slab", "oak_slab_top"}) {
        const BlockId id = *BlockRegistry::instance().findByName(name);
        CHECK_FALSE(isOpaque(id));
        CHECK(blocksLight(id));
        CHECK(isSolidBlock(id));
        CHECK_FALSE(isFullCube(id));
    }
}

TEST_CASE("a slab fills the free half of a cell that already holds one") {
    const BlockId bottom = blockIdOf("oak_slab");
    const BlockId top = blockIdOf("oak_slab_top");

    // Clicking the top of a bottom slab: that face looks into the empty upper half.
    CHECK(combinesIntoDoubleSlab(bottom, bottom, Face::PosY));
    // Clicking the underside of a top slab: the free half is the lower one.
    CHECK(combinesIntoDoubleSlab(bottom, top, Face::NegY));
}

TEST_CASE("a slab does not fill a half that is already taken") {
    const BlockId bottom = blockIdOf("oak_slab");
    const BlockId top = blockIdOf("oak_slab_top");

    // The underside of a bottom slab and the top of a top slab both face *away* from
    // the empty half, so both are ordinary placements into the cell beyond.
    CHECK_FALSE(combinesIntoDoubleSlab(bottom, bottom, Face::NegY));
    CHECK_FALSE(combinesIntoDoubleSlab(bottom, top, Face::PosY));

    // **A side face never combines**, which is vanilla and also the only answer the
    // geometry allows: the ray reaches the side of a bottom slab only within its own
    // lower half, so there is nothing free behind that surface.
    for (const Face side : {Face::NegX, Face::PosX, Face::NegZ, Face::PosZ}) {
        CHECK_FALSE(combinesIntoDoubleSlab(bottom, bottom, side));
        CHECK_FALSE(combinesIntoDoubleSlab(bottom, top, side));
    }
}

TEST_CASE("nothing but a slab combines, and nothing combines with a full block") {
    const BlockId bottom = blockIdOf("oak_slab");
    CHECK_FALSE(combinesIntoDoubleSlab(blockIdOf("stone"), bottom, Face::PosY));
    CHECK_FALSE(combinesIntoDoubleSlab(bottom, blockIdOf("stone"), Face::PosY));
    CHECK_FALSE(combinesIntoDoubleSlab(bottom, blockIdOf("air"), Face::PosY));
}

TEST_CASE("a double slab is a full cube that gives both slabs back") {
    const BlockId dbl = blockIdOf("oak_slab_double");

    // A cube in every respect, so the greedy mesher merges it and the model pass
    // never sees it.
    CHECK(isFullCube(dbl));
    CHECK(isOpaque(dbl));
    CHECK(blocksLight(dbl));
    REQUIRE(blockBoxes(dbl).size() == 1);
    CHECK(blockBoxes(dbl)[0] == BlockBox{0, 0, 0, 16, 16, 16});

    // **The reason it is a block of its own rather than planks**: two back.
    CHECK(dropCountOf(dbl) == 2);
    CHECK(dropOf(dbl, itemIdOf("wooden_axe")) == itemOfBlock(blockIdOf("oak_slab")));

    // And everything else still gives one, which is the no-regression half.
    CHECK(dropCountOf(blockIdOf("stone")) == 1);
    CHECK(dropCountOf(blockIdOf("oak_slab")) == 1);
}
