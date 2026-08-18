#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

namespace mc {

/// The player's collision box: where they stand, how tall they are, and how wide.
///
/// **These numbers lived in `CharacterRenderer` while the camera was the only thing
/// that asked for them, and the cost of that has now been paid twice.** The first
/// time was item pickup, which shipped broken for four play sessions because no test
/// could reach `kPickupRadius` and `kEyeHeight` at once -- `tests/CMakeLists.txt`
/// still names that case. The second was block placement, whose "is this inside the
/// player" test was written from the numbers its caller happened to have: a two-block
/// column at the feet with **no width at all**, which let a player seal a block into
/// their own shoulder.
///
/// They are in `world` now because physics is what asks these questions, and because
/// a rule this engine already wrote down applies directly: a constant only the caller
/// can see cannot be tested. The renderer takes its model height from here rather
/// than the other way round, so what is drawn and what collides cannot drift.
struct PlayerBox {
    /// Eye height, matching Minecraft's 1.62. The camera sits here above the feet.
    static constexpr f32 kEyeHeight = 1.62f;

    /// Feet to the top of the head. Vanilla's player is 1.8; this engine draws and
    /// collides a two-block character, and the two agreeing matters more than either
    /// matching vanilla exactly.
    static constexpr f32 kHeight = 2.0f;

    /// Vanilla's player is 0.6 wide, centred on the feet position -- so the box is
    /// narrower than the block it stands on, which is what lets a player walk into a
    /// one-block gap.
    static constexpr f32 kWidth = 0.6f;
    static constexpr f32 kHalfWidth = kWidth * 0.5f;

    /// Where the feet are. Everything else is derived, so there is one position to
    /// pass and no way to pass the eye by mistake -- which is the whole of the pickup
    /// bug `playerFeet()` exists to prevent.
    vec3 feet{};

    /// Whether the unit cube at `pos` overlaps this box.
    ///
    /// **Touching is not overlapping, and that asymmetry is load bearing.** The block
    /// the player is standing on has its top face exactly at `feet.y`; if that counted
    /// as an overlap, the floor under your own feet would be unbuildable and a player
    /// could not bridge outward from where they stand. The same goes for the four
    /// blocks the box exactly abuts when standing on a grid line.
    bool intersects(BlockPos pos) const noexcept {
        const auto x = static_cast<f32>(pos.x);
        const auto y = static_cast<f32>(pos.y);
        const auto z = static_cast<f32>(pos.z);

        return x + 1.0f > feet.x - kHalfWidth && x < feet.x + kHalfWidth
            && y + 1.0f > feet.y && y < feet.y + kHeight
            && z + 1.0f > feet.z - kHalfWidth && z < feet.z + kHalfWidth;
    }
};

} // namespace mc
