#pragma once

#include "core/Types.hpp"
#include "world/ItemTable.hpp"

namespace mc {

/// What is in one slot.
///
/// **`block` became `item` in Phase 16, and the rename is the feature.** It used to
/// be a `BlockId`, which meant the inventory could hold only things that were blocks
/// -- so a stick could not exist, and neither could a pickaxe. `ItemId` extends the
/// block id space rather than replacing it (see `ItemTable.hpp`), so every stack that
/// used to work still does and the ones that could not now can.
///
/// In its own header since Phase 17, because a slot is no longer something only the
/// player's inventory has: a crafting grid, a furnace and a chest all hold stacks,
/// and none of them should have to include the player to say so.
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

} // namespace mc
