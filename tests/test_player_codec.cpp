#include "world/BlockTable.hpp"
#include "world/ItemTable.hpp"
#include "world/Player.hpp"
#include "world/PlayerCodec.hpp"

#include <doctest/doctest.h>

#include <cstring>

using namespace mc;

namespace {

/// A player with something in every field, so a round trip that drops one is visible.
Player furnished() {
    Player player;
    player.position = {-12.5f, 71.25f, 480.75f};
    player.yaw = 1.25f;
    player.pitch = -0.5f;
    player.health = 13.0f;
    player.flying = true;
    player.onGround = false;
    player.verticalVelocity = -7.5f;
    player.hotbarSlot = 4;

    player.inventory.add(kStoneBlock, 64);
    player.inventory.add(kOakLogBlock, 7);
    player.inventory.mutableAt(Inventory::kStorageSlots - 1) =
        ItemStack{itemIdOf("wooden_pickaxe"), 1};
    return player;
}

} // namespace

TEST_CASE("a player survives the round trip field for field") {
    const Player before = furnished();
    const auto decoded = PlayerCodec::decode(PlayerCodec::encode(before));
    REQUIRE(decoded);
    const Player& after = decoded.value();

    CHECK(after.position.x == doctest::Approx(before.position.x));
    CHECK(after.position.y == doctest::Approx(before.position.y));
    CHECK(after.position.z == doctest::Approx(before.position.z));
    CHECK(after.yaw == doctest::Approx(before.yaw));
    CHECK(after.pitch == doctest::Approx(before.pitch));
    CHECK(after.health == doctest::Approx(before.health));
    CHECK(after.verticalVelocity == doctest::Approx(before.verticalVelocity));
    CHECK(after.flying == before.flying);
    CHECK(after.onGround == before.onGround);
    CHECK(after.hotbarSlot == before.hotbarSlot);

    // Every slot, not just the ones that were filled: a decoder that stopped early
    // would pass a check that only looked where something was put.
    for (usize slot = 0; slot < Inventory::kStorageSlots; ++slot) {
        CAPTURE(slot);
        CHECK(after.inventory.at(slot).item == before.inventory.at(slot).item);
        CHECK(after.inventory.at(slot).count == before.inventory.at(slot).count);
    }
}

TEST_CASE("the cursor is deliberately not saved") {
    // **The one item that could be saved twice.** The cursor holds a stack mid-drag;
    // `Engine::saveEverything` closes the screen first, which returns it to a slot.
    // If the codec wrote it as well, that stack would come back in both places.
    Player player;
    player.inventory.mutableCursor() = ItemStack{kStoneBlock, 32};

    const auto decoded = PlayerCodec::decode(PlayerCodec::encode(player));
    REQUIRE(decoded);
    CHECK(decoded.value().inventory.cursorEmpty());
}

TEST_CASE("a record from another format is refused rather than misread") {
    std::vector<u8> bytes = PlayerCodec::encode(furnished());

    SUBCASE("bad magic") {
        bytes[0] ^= 0xFFu;
        const auto decoded = PlayerCodec::decode(bytes);
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::BadHeader);
    }

    SUBCASE("a version this build does not know") {
        // Refused rather than read as far as it goes. Reading a shorter record as a
        // longer one puts the tail of the inventory wherever the buffer ended, and
        // that failure looks like items vanishing rather than like a format change.
        bytes[4] = PlayerCodec::kVersion + 1;
        const auto decoded = PlayerCodec::decode(bytes);
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::BadHeader);
    }

    SUBCASE("truncated mid-inventory") {
        bytes.resize(bytes.size() - 3);
        const auto decoded = PlayerCodec::decode(bytes);
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::Truncated);
    }

    SUBCASE("empty") {
        const auto decoded = PlayerCodec::decode({});
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::BadHeader);
    }
}

TEST_CASE("a NaN position is refused, because it would never be noticed later") {
    // NaN compares false against everything, so `feet.y < 0` never fires and the
    // ground probe never lands. A player loaded onto a NaN would fall forever with
    // nothing in the log to say why -- which is why this is checked at the boundary
    // rather than trusted and clamped somewhere downstream.
    Player player = furnished();
    player.position.y = std::numeric_limits<f32>::quiet_NaN();

    const auto decoded = PlayerCodec::decode(PlayerCodec::encode(player));
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error() == PlayerCodec::Error::Corrupt);
}

TEST_CASE("values that cannot be true are refused") {
    SUBCASE("a hotbar slot past the hotbar") {
        Player player;
        player.hotbarSlot = Inventory::kHotbarSlots + 3;
        const auto decoded = PlayerCodec::decode(PlayerCodec::encode(player));
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::Corrupt);
    }

    SUBCASE("health past full") {
        Player player;
        player.health = Player::kMaxHealth + 1.0f;
        const auto decoded = PlayerCodec::decode(PlayerCodec::encode(player));
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::Corrupt);
    }

    SUBCASE("an item id past the table") {
        std::vector<u8> bytes = PlayerCodec::encode(furnished());
        // First slot's item id sits right after the header and the scalars.
        const usize slotZero = sizeof(u32) + 1 + 7 * sizeof(f32) + 3;
        const ItemId bogus = kItemCount + 50;
        std::memcpy(bytes.data() + slotZero, &bogus, sizeof(bogus));

        const auto decoded = PlayerCodec::decode(bytes);
        REQUIRE_FALSE(decoded);
        CHECK(decoded.error() == PlayerCodec::Error::Corrupt);
    }
}

TEST_CASE("a stack past its limit is clamped rather than losing the slot") {
    // What a stack limit being lowered between builds looks like: sixty-four of
    // something that now stacks to one. Refusing the record would cost the whole
    // inventory to save the player from an excess of pickaxes.
    Player player;
    player.inventory.mutableAt(0) = ItemStack{itemIdOf("wooden_pickaxe"), 64};

    const auto decoded = PlayerCodec::decode(PlayerCodec::encode(player));
    REQUIRE(decoded);
    CHECK(decoded.value().inventory.at(0).item == itemIdOf("wooden_pickaxe"));
    CHECK(decoded.value().inventory.at(0).count == maxStackOf(itemIdOf("wooden_pickaxe")));
}

TEST_CASE("pitch is re-clamped on the way in") {
    // A record written by a build with a different clamp must not hand back a
    // degenerate view basis. The player is where the pitch is stored, so this is the
    // boundary that has to hold it.
    std::vector<u8> bytes = PlayerCodec::encode(Player{});
    const f32 steep = 3.0f;
    const usize pitchOffset = sizeof(u32) + 1 + 4 * sizeof(f32);
    std::memcpy(bytes.data() + pitchOffset, &steep, sizeof(steep));

    const auto decoded = PlayerCodec::decode(bytes);
    REQUIRE(decoded);
    CHECK(decoded.value().pitch == doctest::Approx(Player::kMaxPitch));
}
