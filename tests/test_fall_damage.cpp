#include "world/FallDamage.hpp"
#include "world/Player.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace mc;

TEST_CASE("a short fall is free") {
    // Three blocks is what makes a jump cost nothing and a one-storey drop safe.
    // Anything that changes this changes how the world feels to move through.
    CHECK(FallDamage::forDistance(0.0f) == 0.0f);
    CHECK(FallDamage::forDistance(1.0f) == 0.0f);
    CHECK(FallDamage::forDistance(FallDamage::kSafeBlocks) == 0.0f);

    // Just past the threshold is still nothing, because the damage is floored --
    // a fall has to clear the *fourth* block to cost anything.
    CHECK(FallDamage::forDistance(3.5f) == 0.0f);
    CHECK(FallDamage::forDistance(3.99f) == 0.0f);
}

TEST_CASE("damage is one half-heart per block past the third") {
    CHECK(FallDamage::forDistance(4.0f) == 1.0f);
    CHECK(FallDamage::forDistance(5.0f) == 2.0f);
    CHECK(FallDamage::forDistance(10.0f) == 7.0f);

    // Floored, so the fractional part of a landing never rounds up into a heart
    // the player did not earn.
    CHECK(FallDamage::forDistance(4.9f) == 1.0f);
    CHECK(FallDamage::forDistance(10.75f) == 7.0f);
}

TEST_CASE("the fatal height matches vanilla's") {
    // **This is the parity claim, and it is the reason this rule is worth a test.**
    // 23 blocks kills a full-health player and 22 does not. A player learns this
    // height in Minecraft and brings it here; getting it wrong is not a crash but a
    // world that lies about what is survivable.
    CHECK(FallDamage::forDistance(22.0f) < Player::kMaxHealth);
    CHECK(FallDamage::forDistance(23.0f) >= Player::kMaxHealth);
}

TEST_CASE("a negative distance costs nothing") {
    // Walking uphill, or a landing above where the fall started. The caller
    // subtracts two heights and does not promise which is larger.
    CHECK(FallDamage::forDistance(-1.0f) == 0.0f);
    CHECK(FallDamage::forDistance(-100.0f) == 0.0f);
}

TEST_CASE("a NaN distance costs nothing rather than poisoning health") {
    // A NaN subtracted from health makes every later comparison false, so the
    // player is neither alive enough to respawn nor dead enough to notice. The
    // guard is written as a negated `>` for exactly this case.
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    CHECK(FallDamage::forDistance(nan) == 0.0f);
}

TEST_CASE("a very long fall saturates rather than overflowing") {
    // Terminal velocity bounds this in practice, but the void does not, and a
    // damage value that wrapped would heal the player instead of killing them.
    CHECK(FallDamage::forDistance(1000.0f) == 997.0f);
    CHECK(FallDamage::forDistance(std::numeric_limits<f32>::infinity())
          > Player::kMaxHealth);
}

// -- Tracking -------------------------------------------------------------------
//
// **The arithmetic above was always right; what was wrong was who called it.** The
// transitions lived as two fields and four assignments inside `Engine::updateWalk`,
// and the jump path was missing one -- so a fall that began with a jump was never
// tracked and landed for free from any height.

TEST_CASE("a fall that begins with a jump is tracked") {
    // **The regression case.** Jumping cleared `onGround` before the substep loop
    // could see it, so this transition never happened and the landing below was free.
    FallTracker fall;
    fall.leftGround(100.0f);
    CHECK(fall.tracking());
    CHECK(fall.landed(80.0f) == doctest::Approx(20.0f));
    CHECK(FallDamage::forDistance(20.0f) == doctest::Approx(17.0f));
}

TEST_CASE("landing without a tracked fall costs nothing") {
    FallTracker fall;
    CHECK(fall.landed(50.0f) == 0.0f);
    CHECK_FALSE(fall.tracking());
}

TEST_CASE("the origin is where the ground was left, not the peak") {
    // A jump therefore costs its own arc, which is why the three-block grace exists.
    FallTracker fall;
    fall.leftGround(64.0f);
    fall.rose(64.8f);   // apex of an ordinary jump
    fall.rose(64.4f);   // rose() only ever raises, so coming back down is ignored
    CHECK(fall.from() == doctest::Approx(64.8f));
    // Landing where it started: the arc is paid for, and it is under the grace.
    CHECK(fall.landed(64.0f) == doctest::Approx(0.8f));
    CHECK(FallDamage::forDistance(0.8f) == 0.0f);
}

TEST_CASE("a second leftGround during one fall does not move the origin") {
    // The substep loop can see "was on the ground" more than once in a frame; a fall
    // has one origin and re-arming it mid-drop would forgive the distance already
    // covered.
    FallTracker fall;
    fall.leftGround(100.0f);
    fall.leftGround(95.0f);
    CHECK(fall.from() == doctest::Approx(100.0f));
    CHECK(fall.landed(90.0f) == doctest::Approx(10.0f));
}

TEST_CASE("water and unloaded ground cancel a fall outright") {
    FallTracker fall;
    fall.leftGround(120.0f);
    fall.cancel();
    CHECK_FALSE(fall.tracking());
    // Landing after a cancel costs nothing, which is what makes jumping off a cliff
    // into a lake a thing people do.
    CHECK(fall.landed(60.0f) == 0.0f);
}

TEST_CASE("rising while nothing is tracked does not start a fall") {
    FallTracker fall;
    fall.rose(200.0f);
    CHECK_FALSE(fall.tracking());
    CHECK(fall.landed(0.0f) == 0.0f);
}

TEST_CASE("landing above where the fall started is not negative damage") {
    // Terrain can arrive under a player, and a ground probe snaps the feet *onto* a
    // surface -- so a landing height above the origin is reachable.
    FallTracker fall;
    fall.leftGround(70.0f);
    CHECK(fall.landed(72.0f) == 0.0f);
}
