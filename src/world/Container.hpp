#pragma once

#include "core/Types.hpp"
#include "world/ItemStack.hpp"

namespace mc {

/// How a slot behaves when it is clicked.
///
/// **Two kinds is the whole taxonomy, and it is not obviously enough until you notice
/// what the second one is for.** A crafting result and a smelted item are not storage:
/// they cannot be put into, they can only be taken, and taking one consumes something
/// else. Every other slot in the game -- storage, a hotbar, a crafting cell, a
/// furnace's fuel and ingredient -- is the same slot with the same rules.
enum class SlotKind : u8 {
    /// Pick up, put down, swap, split. The ordinary slot.
    Normal,
    /// Take-only. Taking it consumes whatever produced it, so it never goes through
    /// the generic pick-and-place path.
    Output,
};

/// Slots that belong to something other than the player, shown in a window while the
/// player is standing at it.
///
/// **This interface is where the second window stops being a special case.** Until
/// Phase 17 there was exactly one window -- the player's own -- and its crafting grid
/// lived inside `Inventory` because there was nowhere else for it to be. A crafting
/// table, a furnace and a chest are three more, and the alternative to an interface
/// here is a `switch` on "which window is open" inside the click routing, the layout
/// and the renderer alike. `BlockTable` exists to keep that same switch from being
/// written for blocks; this is the same argument one layer up.
///
/// The player's own slots are deliberately *not* a `Container`. They are in every
/// window rather than being one, which is why `Screen` composes the two rather than
/// treating them alike.
class Container {
public:
    Container() = default;
    virtual ~Container() = default;

    Container(const Container&) = delete;
    Container& operator=(const Container&) = delete;
    Container(Container&&) = delete;
    Container& operator=(Container&&) = delete;

    /// How many slots this container shows.
    virtual usize slotCount() const = 0;

    virtual SlotKind kindOf(usize slot) const = 0;
    virtual const ItemStack& at(usize slot) const = 0;

    /// Mutable access, for the generic click logic in `Screen`.
    ///
    /// **Never called for an `Output` slot.** Those are taken through `takeOutput`,
    /// which is the only path that also consumes the inputs -- reaching one through
    /// here would hand the player a free pickaxe.
    virtual ItemStack& mutableAt(usize slot) = 0;

    /// Moves the output at `slot` into `cursor`, consuming whatever produced it.
    ///
    /// Does nothing when the hand cannot receive the whole result. That check has to
    /// happen before anything is consumed: crafting into a full hand would either
    /// destroy the result or leave the inputs half-eaten, and both are losses a player
    /// cannot explain.
    virtual void takeOutput(usize slot, ItemStack& cursor) = 0;

    /// Hands back one stack the player must not lose when the window closes, and
    /// clears it. Empty when there is nothing left to return; the caller calls again
    /// until it is.
    ///
    /// **A chest returns nothing and a crafting grid returns everything**, and that
    /// difference is the whole of why this is virtual rather than a rule `Screen`
    /// applies to all containers. What is in a chest stays in the chest.
    virtual ItemStack releaseOne() = 0;
};

} // namespace mc
