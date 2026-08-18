#pragma once

#include "core/Types.hpp"
#include "world/Container.hpp"
#include "world/Inventory.hpp"

namespace mc {

/// The window that is currently open: a container's slots, the player's own, and the
/// stack in the player's hand, in one flat index space.
///
/// **One index space rather than three, and that is the same call `Inventory` made
/// when it had a crafting grid inside it.** A hit test returns one kind of answer, the
/// renderer walks one range, and "which grid is this" is decided here instead of in
/// every caller. What changed in Phase 17 is only *where* the index space is composed:
/// it used to be baked into `Inventory`, which is why there could be exactly one
/// window.
///
/// Container slots come first and the player's thirty-six follow, which is the order
/// vanilla draws them in and means a container's own indices are its own -- a chest's
/// slot 0 is its top-left whether or not a player is looking at it.
///
/// **The cursor stays in `Inventory` rather than living here.** A window that owns the
/// stack in the player's hand can lose it by closing, and this one closes every time
/// the player walks away from a table.
class Screen {
public:
    Screen(Inventory& inventory, Container& container) noexcept
        : m_inventory(&inventory), m_container(&container) {}

    usize containerSlots() const { return m_container->slotCount(); }
    usize slotCount() const { return containerSlots() + Inventory::kStorageSlots; }

    /// Whether `slot` belongs to the container rather than to the player.
    bool isContainerSlot(usize slot) const { return slot < containerSlots(); }

    const ItemStack& at(usize slot) const;
    SlotKind kindOf(usize slot) const;

    const Inventory& inventory() const noexcept { return *m_inventory; }
    const Container& container() const noexcept { return *m_container; }

    /// A left click: pick the stack up, put it down, merge it, or take an output.
    ///
    /// One entry point rather than pick/place/swap, because which of those a click
    /// means is decided by what is already in the hand and in the slot. Splitting it
    /// would put that decision in the caller, where the window and every future
    /// container would each have to get it right separately.
    void click(usize slot);

    /// A right click: take half when the hand is empty, put down one when it is not.
    /// On an output slot it takes once, same as a left click -- there is no half of a
    /// pickaxe.
    void split(usize slot);

    /// What one step of closing the window did.
    ///
    /// **Two fields because one cannot say both things, and the first attempt at this
    /// proved it.** `releaseOne` used to return just the spilled stack, so an empty
    /// return meant either "there is nothing left to give back" or "it went into
    /// storage and nothing spilled" -- and the caller's loop, which stopped on empty,
    /// gave back the first crafting cell and silently deleted the rest. That is the
    /// same shape of bug as `blockAt` answering air for a column that is not loaded:
    /// two different answers collapsed into one value.
    struct Release {
        /// Whether anything was handed back at all. False is the caller's signal to
        /// stop; it is the only honest "done".
        bool moved = false;
        /// What storage had no room for, for the caller to drop into the world.
        /// Empty when everything fit, which is the ordinary case.
        ItemStack spilled{};
    };

    /// Gives back one stack the player must not lose, having tried to put it into
    /// storage first. The caller drops `spilled` and calls again until `moved` is
    /// false.
    ///
    /// **Called on the way out, and it covers two different losses.** The stack in the
    /// hand, and whatever the container says must come back -- a crafting grid's
    /// inputs, and nothing at all from a chest.
    Release releaseOne();

private:
    /// The slot a flat index names, for the ordinary pick-and-place path. Null for an
    /// output slot, which never goes through it.
    ItemStack* mutableAt(usize slot);

    Inventory* m_inventory;
    Container* m_container;
};

} // namespace mc
