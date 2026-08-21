#include "world/BlockTable.hpp"
#include "world/ItemTable.hpp"
#include "world/Player.hpp"

#include <doctest/doctest.h>

using namespace mc;

TEST_CASE("the player holds feet and the camera is told the eye") {
    // The direction that matters. `PlayerBox`'s header records what it cost to have
    // the camera hold the position and everything else convert: item pickup shipped
    // broken for four sessions because one caller passed the eye where the feet were
    // wanted. The player owning the feet is what makes that unrepresentable.
    Player player;
    player.position = {1.0f, 64.0f, -3.0f};

    CHECK(player.eye().y == doctest::Approx(64.0f + PlayerBox::kEyeHeight));
    CHECK(player.eye().x == doctest::Approx(1.0f));
    CHECK(player.eye().z == doctest::Approx(-3.0f));

    // And the box is built from the same number, so what collides and what is drawn
    // cannot drift from where the camera is.
    CHECK(player.box().feet.y == doctest::Approx(64.0f));
}

TEST_CASE("pitch cannot reach the pole no matter how far the mouse is dragged") {
    // At exactly pi/2 the forward vector is parallel to world up and the view basis
    // degenerates. The clamp lives here rather than only in `Camera` because this is
    // where the pitch is stored -- a camera synced from an unclamped player would be
    // handed the degenerate value and clamp it silently, leaving the two disagreeing.
    Player player;

    player.rotate(0.0f, 100.0f);
    CHECK(player.pitch == doctest::Approx(Player::kMaxPitch));

    player.rotate(0.0f, -1000.0f);
    CHECK(player.pitch == doctest::Approx(-Player::kMaxPitch));

    // Yaw does not clamp -- it wraps naturally through the trigonometry and a player
    // may spin as many times as they like.
    player.yaw = 0.0f;
    player.rotate(100.0f, 0.0f);
    CHECK(player.yaw == doctest::Approx(100.0f));
}

TEST_CASE("an empty hand is kNoItem rather than whatever slot zero holds") {
    Player player;
    CHECK(player.heldItem() == kNoItem);

    player.inventory.add(kStoneBlock, 1);
    player.hotbarSlot = 0;
    CHECK(player.heldItem() == kStoneBlock);

    // A slot the player is not pointing at is not in their hand.
    player.hotbarSlot = 5;
    CHECK(player.heldItem() == kNoItem);
}

TEST_CASE("a fresh player is at full health and not dead") {
    Player player;
    CHECK(player.health == doctest::Approx(Player::kMaxHealth));
    CHECK(player.health == doctest::Approx(20.0f)); // vanilla's ten hearts of two
    CHECK_FALSE(player.dead());

    player.health = 0.0f;
    CHECK(player.dead());
}
