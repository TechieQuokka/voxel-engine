#include "world/Inventory.hpp"

#include "core/Assert.hpp"

#include <algorithm>

namespace mc {
namespace {

/// An empty stack out of line, so `at()` can return a reference for an out-of-range
/// slot rather than asserting. Callers that index from a hit test have already been
/// told the slot exists; this is for the ones that have not.
const ItemStack kNothing{};

} // namespace

const ItemStack& Inventory::at(usize slot) const {
    return slot < kSlotCount ? m_slots[slot] : kNothing;
}

u32 Inventory::add(ItemId item, u32 count) {
    if (!itemExists(item) || count == 0) {
        return count;
    }

    const u32 limit = maxStackOf(item);
    u32 remaining = count;

    // Storage only. `kStorageSlots` rather than `kSlotCount` is what keeps a
    // picked-up stack out of the crafting grid, where it would silently change what
    // the grid produces.
    //
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
    if (slot >= kSlotCount || m_slots[slot].empty()) {
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

std::array<ItemId, Inventory::kCraftSlots> Inventory::craftIds() const {
    std::array<ItemId, kCraftSlots> ids{};
    for (usize i = 0; i < kCraftSlots; ++i) {
        const ItemStack& stack = m_slots[kFirstCraftSlot + i];
        ids[i] = stack.empty() ? kNoItem : stack.item;
    }
    return ids;
}

CraftResult Inventory::craftResult() const {
    return matchRecipe(craftIds());
}

void Inventory::takeCraftOutput() {
    const CraftResult result = craftResult();
    if (result.empty()) {
        return;
    }

    // The hand has to be able to receive the whole output before anything is
    // consumed. Crafting into a hand with no room would either destroy the result or
    // leave the grid half-eaten, and both are the kind of loss a player cannot
    // explain.
    if (!m_cursor.empty()) {
        if (m_cursor.item != result.item) {
            return;
        }
        if (m_cursor.count + result.count > maxStackOf(result.item)) {
            return;
        }
    }

    for (usize i = 0; i < kCraftSlots; ++i) {
        ItemStack& stack = m_slots[kFirstCraftSlot + i];
        if (stack.empty()) {
            continue;
        }
        // One from each occupied cell, whatever the stack depth. That is what lets a
        // grid of full stacks be clicked repeatedly rather than crafting once and
        // needing to be refilled.
        if (--stack.count == 0) {
            stack.clear();
        }
    }

    m_cursor.item = result.item;
    m_cursor.count += result.count;
}

void Inventory::clickSlot(usize slot) {
    if (slot >= kSlotCount) {
        return;
    }
    if (slot == kOutputSlot) {
        takeCraftOutput();
        return;
    }

    ItemStack& target = m_slots[slot];

    if (m_cursor.empty()) {
        // Nothing in hand: pick the whole stack up. An empty slot leaves the hand
        // empty, which is a click that does nothing rather than one that errors.
        m_cursor = target;
        target.clear();
        return;
    }

    if (target.empty()) {
        target = m_cursor;
        m_cursor.clear();
        return;
    }

    if (target.item == m_cursor.item) {
        // Same type: pour as much as fits and keep the rest in hand. Keeping the
        // remainder rather than refusing is what lets a player top a stack up from a
        // bigger one without first finding somewhere to put the difference.
        const u32 limit = maxStackOf(target.item);
        const u32 room = target.count < limit ? limit - target.count : 0u;
        const u32 moved = std::min(room, m_cursor.count);
        target.count += moved;
        m_cursor.count -= moved;
        if (m_cursor.count == 0) {
            m_cursor.clear();
        }
        return;
    }

    std::swap(target, m_cursor);
}

void Inventory::splitSlot(usize slot) {
    if (slot >= kSlotCount) {
        return;
    }
    if (slot == kOutputSlot) {
        // There is no half of a crafted result to take. Vanilla crafts one here too.
        takeCraftOutput();
        return;
    }

    ItemStack& target = m_slots[slot];

    if (m_cursor.empty()) {
        if (target.empty()) {
            return;
        }
        // The larger half stays in the hand, which is vanilla's rounding and is the
        // one people expect when splitting an odd stack.
        const u32 taken = (target.count + 1) / 2;
        m_cursor.item = target.item;
        m_cursor.count = taken;
        target.count -= taken;
        if (target.count == 0) {
            target.clear();
        }
        return;
    }

    // Something in hand: put down exactly one, which is how a stack gets spread
    // across slots one at a time. **This is the click that fills a crafting grid**,
    // and it is why the grid needed no interaction of its own.
    if (target.empty()) {
        target.item = m_cursor.item;
        target.count = 1;
    } else if (target.item == m_cursor.item && target.count < maxStackOf(target.item)) {
        ++target.count;
    } else {
        return; // A different item, or a full stack. Neither takes one more.
    }

    if (--m_cursor.count == 0) {
        m_cursor.clear();
    }
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

ItemStack Inventory::releaseCraftGrid() {
    for (usize i = 0; i < kCraftSlots; ++i) {
        ItemStack& stack = m_slots[kFirstCraftSlot + i];
        if (stack.empty()) {
            continue;
        }
        const u32 leftover = add(stack.item, stack.count);
        if (leftover > 0) {
            // Storage is full. Hand this one back and leave the rest where it is --
            // the caller drops what it gets and calls again, so nothing is lost even
            // when several cells cannot fit.
            const ItemStack spilled{stack.item, leftover};
            stack.clear();
            return spilled;
        }
        stack.clear();
    }
    return ItemStack{};
}

} // namespace mc
