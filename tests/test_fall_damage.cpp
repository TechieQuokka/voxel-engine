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
