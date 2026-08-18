#include "world/Inventory.hpp"

#include <algorithm>

namespace mc {
namespace {

/// An empty stack out of line, so `at()` can return a reference for an out-of-range
/// slot rather than asserting. Callers that index from a hit test have already been
/// told the slot exists; this is for the ones that have not.
///
/// **`mutableAt` returns it too, which means a write to an out-of-range slot goes
/// nowhere rather than out of bounds.** That is the safe failure and not a useful
/// one, so `Screen` bounds-checks before it ever gets here -- this is the floor under
/// that check, not a substitute for it.
ItemStack kNothing{};

} // namespace

const ItemStack& Inventory::at(usize slot) const {
    return slot < kStorageSlots ? m_slots[slot] : kNothing;
}

ItemStack& Inventory::mutableAt(usize slot) {
    return slot < kStorageSlots ? m_slots[slot] : kNothing;
}

u32 Inventory::add(ItemId item, u32 count) {
    if (!itemExists(item) || count == 0) {
        return count;
    }

    const u32 limit = maxStackOf(item);
    u32 remaining = count;

    // Existing partial stacks first, so collecting does not scatter one item type
    // across slots while a half-full stack of it is already held.
    for (usize i = 0; i < kStorageSlots && remaining > 0; ++i) {
        ItemStack& stack = m_slots[i];
        if (stack.item != item || stack.count == 0 || stack.count >= limit) {
            continue;
        }
        const u32 moved = std::min(limit - stack.count, remaining);
        stack.count += moved;
        remaining -= moved;
    }

    // Then empty slots. The array is ordered hotbar-first, so this fills the row the
    // player can actually reach before the grid they would have to open a window for.
    for (usize i = 0; i < kStorageSlots && remaining > 0; ++i) {
        ItemStack& stack = m_slots[i];
        if (!stack.empty()) {
            continue;
        }
        const u32 moved = std::min(limit, remaining);
        stack.item = item;
        stack.count = moved;
        remaining -= moved;
    }

    return remaining;
}

bool Inventory::takeOne(usize slot) {
    if (slot >= kStorageSlots || m_slots[slot].empty()) {
        return false;
    }
    if (--m_slots[slot].count == 0) {
        m_slots[slot].clear();
    }
    return true;
}

u32 Inventory::count(ItemId item) const {
    if (item == kNoItem) {
        return 0;
    }
    u32 total = 0;
    for (usize i = 0; i < kStorageSlots; ++i) {
        if (m_slots[i].item == item) {
            total += m_slots[i].count;
        }
    }
    return total;
}

usize Inventory::usedSlots() const {
    usize used = 0;
    for (usize i = 0; i < kStorageSlots; ++i) {
        used += m_slots[i].empty() ? 0u : 1u;
    }
    return used;
}

ItemStack Inventory::releaseCursor() {
    if (m_cursor.empty()) {
        return ItemStack{};
    }

    const ItemId item = m_cursor.item;
    const u32 leftover = add(item, m_cursor.count);
    m_cursor.clear();

    // Whatever the inventory had no room for. The caller drops it, because the
    // alternative -- deleting it because a window closed -- is the kind of loss a
    // player notices and cannot explain.
    return leftover > 0 ? ItemStack{item, leftover} : ItemStack{};
}

} // namespace mc
