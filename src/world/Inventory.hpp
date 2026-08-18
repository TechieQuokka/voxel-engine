#pragma once

#include "core/Types.hpp"
#include "world/ItemStack.hpp"

#include <array>

namespace mc {

/// The player's slots, and the stack in their hand.
///
/// **This replaced a count-per-block-type model, by decision rather than by decay.**
/// The old one closed the whole break-drop-collect-place loop for a fraction of the
/// work, and the argument for it was right: the loop does work. What it got wrong is
/// that *carrying things* is not felt as a number going up. The container is the
/// feature, and nine numbers on a hotbar is the bookkeeping behind a container rather
/// than the thing itself. That judgement came out of playing it, which is where the
/// last several changes of direction in this project have come from.
///
/// Vanilla's shape, because the point is to feel like the game: 36 slots, the first
/// nine of which are the hotbar. That ordering is not decoration -- it is why a
/// picked-up stack lands somewhere the player can immediately use.
///
/// **Stack limits are what make an inventory fill up**, and an inventory that cannot
/// fill up is a list with extra steps. 64 is vanilla's for most things and 1 for
/// tools, and it is what crafting needs when a recipe has to ask whether the output
/// fits.
///
/// **The crafting grid used to be in here, and Phase 17 took it out.** It was here
/// because there was exactly one window and nowhere else to put it; a crafting table
/// is a second window, so the grid is a `CraftingGrid` the player owns and `Screen`
/// composes. What is left is what genuinely belongs to the player wherever they are
/// standing: thirty-six slots and one hand.
///
/// The cursor stays here for that reason. A window that owns the stack in the hand can
/// lose it by closing, and windows now close often.
class Inventory {
public:
    static constexpr usize kHotbarSlots = 9;
    static constexpr usize kMainSlots = 27;
    static constexpr usize kStorageSlots = kHotbarSlots + kMainSlots;

    /// Kept for the callers that mean "a normal stack". Per-item limits come from
    /// `ItemStack::stackLimit`, which is what the inventory itself uses.
    static constexpr u32 kMaxStack = 64;

    /// Adds what it can and returns what did not fit.
    ///
    /// Vanilla's order, and each step is there for a reason a player would notice:
    /// existing partial stacks first so picking up does not fragment what is already
    /// held, then empty slots, and the hotbar before the main grid at both steps so
    /// that what you just collected is reachable without opening anything.
    u32 add(ItemId item, u32 count);

    /// Removes one from `slot`. False when it was already empty, which is the caller's
    /// signal not to place anything.
    bool takeOne(usize slot);

    const ItemStack& at(usize slot) const;

    /// Mutable access for `Screen`'s click routing, which moves stacks between the
    /// player's slots and whatever container is open. Nothing else should need it:
    /// pickup goes through `add`, and placing goes through `takeOne`.
    ItemStack& mutableAt(usize slot);

    /// Total of `item` across every slot. For the "do I have any" question, which does
    /// not care where.
    u32 count(ItemId item) const;

    usize usedSlots() const;

    // -- the cursor -----------------------------------------------------------
    //
    // The stack the player has picked up and is dragging. It lives here rather than
    // in the screen that draws it, because it holds real items: a UI that owns them
    // can lose them by being closed, and this one cannot.

    const ItemStack& cursor() const noexcept { return m_cursor; }
    bool cursorEmpty() const noexcept { return m_cursor.empty(); }
    ItemStack& mutableCursor() noexcept { return m_cursor; }

    /// Puts the cursor stack back into the inventory, and returns whatever did not
    /// fit so the caller can drop it into the world. Called when a screen closes: a
    /// stack in the hand when the window shuts must not simply evaporate.
    ItemStack releaseCursor();

private:
    std::array<ItemStack, kStorageSlots> m_slots{};
    ItemStack m_cursor{};
};

} // namespace mc
