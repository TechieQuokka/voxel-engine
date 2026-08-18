#include "world/PlayerBox.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

/// Standing in the middle of the block at (8, 64, 8), which is the case every other
/// assertion here is a displacement of.
PlayerBox standing() {
    return PlayerBox{vec3{8.5f, 64.0f, 8.5f}};
}

} // namespace

TEST_CASE("the block underfoot is not inside the player") {
    // The whole reason `intersects` treats touching as clear. A player standing on a
    // block has its top face exactly at their feet, and if that counted as an overlap
    // the floor could never be repaired from where you stand.
    CHECK_FALSE(standing().intersects(BlockPos{8, 63, 8}));
}

TEST_CASE("the blocks the player occupies are refused") {
    const PlayerBox box = standing();

    // Feet and head. A two-block-tall player fills exactly these two cells when
    // standing in the centre of a column.
    CHECK(box.intersects(BlockPos{8, 64, 8}));
    CHECK(box.intersects(BlockPos{8, 65, 8}));

    // And nothing above the head.
    CHECK_FALSE(box.intersects(BlockPos{8, 66, 8}));
}

TEST_CASE("a block beside the player is free when the box does not reach it") {
    const PlayerBox box = standing();

    // The box is 0.6 wide centred at 8.5, so it spans [8.2, 8.8] and touches neither
    // neighbouring column. This is what makes a one-block gap walkable, and it is
    // also what lets a player wall themselves in from the inside.
    CHECK_FALSE(box.intersects(BlockPos{7, 64, 8}));
    CHECK_FALSE(box.intersects(BlockPos{9, 64, 8}));
    CHECK_FALSE(box.intersects(BlockPos{8, 64, 7}));
    CHECK_FALSE(box.intersects(BlockPos{8, 64, 9}));
}

TEST_CASE("standing on a block boundary the player straddles four columns") {
    // **This is the case the old placement test could not see at all.** It compared
    // integer feet coordinates, so a player standing exactly on a corner was treated
    // as occupying one column -- and a block placed into any of the other three went
    // straight through their shoulder.
    const PlayerBox box{vec3{8.0f, 64.0f, 8.0f}};

    CHECK(box.intersects(BlockPos{7, 64, 7}));
    CHECK(box.intersects(BlockPos{7, 64, 8}));
    CHECK(box.intersects(BlockPos{8, 64, 7}));
    CHECK(box.intersects(BlockPos{8, 64, 8}));

    // One column further out in any direction is clear again.
    CHECK_FALSE(box.intersects(BlockPos{6, 64, 7}));
    CHECK_FALSE(box.intersects(BlockPos{7, 64, 9}));
}

TEST_CASE("a partial step up puts the player in three vertical cells") {
    // Mid-jump, or standing on something that is not on the block grid. The head is
    // above y=66 now, so the cell the player's hair is in has to be refused too --
    // the old two-cell test would have allowed a block there.
    const PlayerBox box{vec3{8.5f, 64.5f, 8.5f}};

    CHECK(box.intersects(BlockPos{8, 64, 8}));
    CHECK(box.intersects(BlockPos{8, 65, 8}));
    CHECK(box.intersects(BlockPos{8, 66, 8}));
    CHECK_FALSE(box.intersects(BlockPos{8, 63, 8}));
    CHECK_FALSE(box.intersects(BlockPos{8, 67, 8}));
}

TEST_CASE("the eye is inside the box and the box is vanilla's width") {
    // The relationship that item pickup got wrong, pinned where a test can see it:
    // the eye is a height within the body, not the body's position.
    CHECK(PlayerBox::kEyeHeight < PlayerBox::kHeight);
    CHECK(PlayerBox::kEyeHeight > 0.0f);

    // Narrower than the block it stands on, which is what a one-block doorway needs.
    CHECK(PlayerBox::kWidth < 1.0f);
    CHECK(PlayerBox::kHalfWidth * 2.0f == doctest::Approx(PlayerBox::kWidth));
}
