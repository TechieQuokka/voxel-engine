#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Inventory.hpp"
#include "world/PlayerBox.hpp"

namespace mc {

/// Everything that *is* the player, as opposed to everything drawn of them.
///
/// **This type exists to answer one question that had no owner: what gets saved.**
/// The state below lived as fourteen loose members on `Engine`, mixed in among the
/// streaming pipeline and the swing animation, and nothing said which of them a save
/// file should carry. The answer was a judgement call made by reading a member list --
/// which is exactly the kind of decision that goes wrong once and then stays wrong.
///
/// The rule here is: **if it is in this struct it is saved, and if it is not it is
/// rebuilt.** Adding a field is therefore a decision about persistence whether the
/// author wants it to be or not, which is the property the loose members did not have.
///
/// Deliberately *not* here, and each for a reason:
///
/// - **The swing, the walk cycle and the break progress.** Animation and an action in
///   flight. Restoring a half-swung arm or a half-mined block would be restoring the
///   middle of an input the player is no longer giving.
/// - **Third person.** A camera preference, not a fact about the player. It belongs
///   with settings if it is ever persisted at all.
/// - **The 2x2 craft grid.** Vanilla drops what is in a crafting grid when the window
///   closes; keeping it would be a deviation, and an invisible one.
/// - **The aimed-at block, and the open screen.** Derived from where the player is
///   looking and what they clicked, both of which are gone by the next run.
///
/// The camera is a *view* of this, not a second copy: it is synced from `position`
/// and the orientation below, never the other way round. That direction is what the
/// pickup bug in `PlayerBox`'s header cost this project once already -- the camera
/// holds the eye, the player holds the feet, and the two must not both be writable.
struct Player {
    /// Full health, and vanilla's: ten hearts of two.
    static constexpr f32 kMaxHealth = 20.0f;

    /// Just under pi/2. At exactly pi/2 the forward vector is parallel to world up and
    /// the view basis degenerates. `Camera` clamps to the same value; this is the copy
    /// that matters, because this is where the pitch is actually stored.
    static constexpr f32 kMaxPitch = 1.5533f;

    /// **Feet, never the eye.** `PlayerBox::kEyeHeight` is the conversion, and
    /// `eye()` below is the only place it should be applied.
    vec3 position{0.0f, 0.0f, 0.0f};

    /// Radians.
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;

    f32 health = kMaxHealth;

    /// Flying is saved because vanilla saves it: coming back from a flying session
    /// walking, mid-air, is a fall the player did not ask for.
    bool flying = false;

    /// Saved with the vertical velocity so that quitting mid-fall and returning
    /// resumes the fall rather than restarting it at rest a block above the ground.
    bool onGround = false;
    f32 verticalVelocity = 0.0f;

    Inventory inventory;

    /// Which of the nine hotbar slots is selected. Held separately from the inventory
    /// because it is a property of the player rather than of the container -- a chest
    /// has slots and no selection.
    usize hotbarSlot = 0;

    /// Where the camera goes.
    vec3 eye() const noexcept {
        return {position.x, position.y + PlayerBox::kEyeHeight, position.z};
    }

    PlayerBox box() const noexcept { return PlayerBox{position}; }

    /// Applies a mouse delta, clamping pitch so the basis cannot degenerate.
    void rotate(f32 deltaYaw, f32 deltaPitch) noexcept {
        yaw += deltaYaw;
        pitch = math::clamp(pitch + deltaPitch, -kMaxPitch, kMaxPitch);
    }

    bool dead() const noexcept { return health <= 0.0f; }

    /// What is in the hand, or `kNoItem` for an empty one.
    ///
    /// A named accessor rather than `inventory.at(hotbarSlot).item` at each call site,
    /// for the reason `eye()` exists: several callers ask this -- break time, the drop
    /// test, placement, what to draw in the fist -- and the next one written is where
    /// they would start to disagree about an empty slot.
    ItemId heldItem() const {
        const ItemStack& held = inventory.at(hotbarSlot);
        return held.empty() ? kNoItem : held.item;
    }
};

} // namespace mc
