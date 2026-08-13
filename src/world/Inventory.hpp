#pragma once

#include "core/Types.hpp"
#include "world/Crafting.hpp"
#include "world/ItemTable.hpp"

#include <array>

namespace mc {

/// What is in one slot.
///
/// **`block` became `item` in Phase 16, and the rename is the feature.** It used to
/// be a `BlockId`, which meant the inventory could hold only things that were blocks
/// -- so a stick could not exist, and neither could a pickaxe. `ItemId` extends the
/// block id space rather than replacing it (see `ItemTable.hpp`), so every stack that
/// used to work still does and the ones that could not now can.
struct ItemStack {
    ItemId item = kNoItem;
    u32 count = 0;

    bool empty() const noexcept { return count == 0 || item == kNoItem; }
    void clear() noexcept {
        item = kNoItem;
        count = 0;
    }

    /// How many of this item fit in one slot. **Per item, not a constant**: tools
    /// stack to one, because sixty-four pickaxes in a slot is a pickaxe that never
    /// runs out and leaves Phase 17's durability nowhere to live.
    u32 stackLimit() const noexcept { return maxStackOf(item); }
};

/// The player's slots, their crafting grid, and the stack in their hand.
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
/// **The crafting grid lives here rather than in the screen that draws it**, for the
/// same reason the cursor does and it is the same reason twice: both hold real items,
/// and a UI that owns them can lose them by being closed. `releaseCraftGrid` is what
/// the screen calls on the way out.
class Inventory {
public:
    static constexpr usize kHotbarSlots = 9;
    static constexpr usize kMainSlots = 27;
    static constexpr usize kStorageSlots = kHotbarSlots + kMainSlots;

    /// The 3x3 grid. See `Crafting.hpp` for why it is 3x3 in the player's own window
    /// when vanilla's is 2x2.
    static constexpr usize kCraftSlots = 9;

    /// Slot indices run storage, then the craft grid, then the single output slot.
    /// **One index space rather than three**, so `clickSlot` is one function and the
    /// hit test returns one kind of answer -- the alternative puts "which grid is
    /// this" in every caller, and there are already two callers.
    static constexpr usize kFirstCraftSlot = kStorageSlots;
    static constexpr usize kOutputSlot = kFirstCraftSlot + kCraftSlots;
    static constexpr usize kSlotCount = kOutputSlot + 1;

    /// Kept for the callers that mean "a normal stack". Per-item limits come from
    /// `ItemStack::stackLimit`, which is what the inventory itself uses.
    static constexpr u32 kMaxStack = 64;

    /// Adds what it can and returns what did not fit. **Storage slots only** -- the
    /// craft grid is somewhere the player puts things, never somewhere they land.
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

    /// Total of `item` across every storage slot. For the "do I have any" question,
    /// which does not care where.
    u32 count(ItemId item) const;

    usize usedSlots() const;

    // -- crafting -------------------------------------------------------------

    /// What the grid currently produces, or an empty result.
    CraftResult craftResult() const;

    /// Puts the craft grid's contents back into storage and returns whatever did not
    /// fit, for the caller to drop. Called when the screen closes: nine slots of
    /// materials must not evaporate because a window shut.
    ///
    /// Returns one stack at most, because the grid holds at most nine and storage is
    /// thirty-six -- a grid that does not fit means the pack was already full, and
    /// the caller drops what comes back and calls again until it returns empty.
    ItemStack releaseCraftGrid();

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
    ///
    /// **The output slot is the one that behaves differently**, and it has to: it is
    /// not storage, it is a preview of what the grid would make. Clicking it crafts.
    void clickSlot(usize slot);

    /// A right click: take half when the hand is empty, put down one when it is not.
    /// Vanilla's, and the half-split is the only way to divide a stack without a
    /// number entry field. On the output slot it crafts once, same as a left click --
    /// there is no half of a pickaxe to take.
    void splitSlot(usize slot);

    /// Puts the cursor stack back into the inventory, and returns whatever did not
    /// fit so the caller can drop it into the world. Called when the screen closes:
    /// a stack in the hand when the window shuts must not simply evaporate.
    ItemStack releaseCursor();

private:
    /// Takes the output once: hands it to the cursor and consumes one from every
    /// occupied grid cell. Does nothing when the hand cannot receive the result,
    /// which is what stops a click crafting into a full hand and losing the output.
    void takeCraftOutput();

    /// The grid as ids, which is all `matchRecipe` needs.
    std::array<ItemId, kCraftSlots> craftIds() const;

    std::array<ItemStack, kSlotCount> m_slots{};
    ItemStack m_cursor{};
};

} // namespace mc
