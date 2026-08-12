#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"

#include <array>

namespace mc {

/// What is in one slot.
struct ItemStack {
    BlockId block = kAirBlock;
    u32 count = 0;

    bool empty() const noexcept { return count == 0 || block == kAirBlock; }
    void clear() noexcept {
        block = kAirBlock;
        count = 0;
    }
};

/// The player's slots.
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
/// fill up is a list with extra steps. 64 is vanilla's, and it is also what crafting
/// will need when a recipe has to ask whether the output fits.
class Inventory {
public:
    static constexpr usize kHotbarSlots = 9;
    static constexpr usize kMainSlots = 27;
    static constexpr usize kSlotCount = kHotbarSlots + kMainSlots;
    static constexpr u32 kMaxStack = 64;

    /// Adds what it can and returns what did not fit.
    ///
    /// Vanilla's order, and each step is there for a reason a player would notice:
    /// existing partial stacks first so picking up does not fragment what is already
    /// held, then empty slots, and the hotbar before the main grid at both steps so
    /// that what you just collected is reachable without opening anything.
    u32 add(BlockId block, u32 count);

    /// Removes one from `slot`. False when it was already empty, which is the caller's
    /// signal not to place anything.
    bool takeOne(usize slot);

    const ItemStack& at(usize slot) const;

    /// Total of `block` across every slot. For the "do I have any" question, which
    /// does not care where.
    u32 count(BlockId block) const;

    usize usedSlots() const;

    // -- the cursor -----------------------------------------------------------
    //
    // The stack the player has picked up and is dragging. It lives here rather than
    // in the screen that draws it, because it holds real items: a UI that owns them
    // can lose them by being closed, and this one cannot.

    const ItemStack& cursor() const noexcept { return m_cursor; }
    bool cursorEmpty() const noexcept { return m_cursor.empty(); }

    /// A left click on a slot: pick the stack up, put it down, or merge it.
    ///
    /// One entry point rather than pick/place/swap, because which of those a click
    /// means is decided by what is already in the hand and in the slot, and splitting
    /// it across three methods would put that decision in the caller -- where the
    /// screen and any future container would each have to get it right separately.
    void clickSlot(usize slot);

    /// A right click: take half when the hand is empty, put down one when it is not.
    /// Vanilla's, and the half-split is the only way to divide a stack without a
    /// number entry field.
    void splitSlot(usize slot);

    /// Puts the cursor stack back into the inventory, and returns whatever did not
    /// fit so the caller can drop it into the world. Called when the screen closes:
    /// a stack in the hand when the window shuts must not simply evaporate.
    ItemStack releaseCursor();

private:
    std::array<ItemStack, kSlotCount> m_slots{};
    ItemStack m_cursor{};
};

} // namespace mc
