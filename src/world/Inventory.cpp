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

u32 Inventory::add(BlockId block, u32 count) {
    if (block == kAirBlock || block >= kBlocks.size() || count == 0) {
        return count;
    }

    u32 remaining = count;

    // Existing partial stacks first, so collecting does not scatter one block type
    // across slots while a half-full stack of it is already held.
    for (ItemStack& stack : m_slots) {
        if (remaining == 0) {
            break;
        }
        if (stack.block != block || stack.count == 0 || stack.count >= kMaxStack) {
            continue;
        }
        const u32 room = kMaxStack - stack.count;
        const u32 moved = std::min(room, remaining);
        stack.count += moved;
        remaining -= moved;
    }

    // Then empty slots. The array is ordered hotbar-first, so this fills the row the
    // player can actually reach before the grid they would have to open a window for.
    for (ItemStack& stack : m_slots) {
        if (remaining == 0) {
            break;
        }
        if (!stack.empty()) {
            continue;
        }
        const u32 moved = std::min(kMaxStack, remaining);
        stack.block = block;
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

u32 Inventory::count(BlockId block) const {
    if (block == kAirBlock) {
        return 0;
    }
    u32 total = 0;
    for (const ItemStack& stack : m_slots) {
        if (stack.block == block) {
            total += stack.count;
        }
    }
    return total;
}

usize Inventory::usedSlots() const {
    usize used = 0;
    for (const ItemStack& stack : m_slots) {
        used += stack.empty() ? 0u : 1u;
    }
    return used;
}

void Inventory::clickSlot(usize slot) {
    if (slot >= kSlotCount) {
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

    if (target.block == m_cursor.block) {
        // Same type: pour as much as fits and keep the rest in hand. Keeping the
        // remainder rather than refusing is what lets a player top a stack up from a
        // bigger one without first finding somewhere to put the difference.
        const u32 room = target.count < kMaxStack ? kMaxStack - target.count : 0u;
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
    ItemStack& target = m_slots[slot];

    if (m_cursor.empty()) {
        if (target.empty()) {
            return;
        }
        // The larger half stays in the hand, which is vanilla's rounding and is the
        // one people expect when splitting an odd stack.
        const u32 taken = (target.count + 1) / 2;
        m_cursor.block = target.block;
        m_cursor.count = taken;
        target.count -= taken;
        if (target.count == 0) {
            target.clear();
        }
        return;
    }

    // Something in hand: put down exactly one, which is how a stack gets spread
    // across slots one at a time.
    if (target.empty()) {
        target.block = m_cursor.block;
        target.count = 1;
    } else if (target.block == m_cursor.block && target.count < kMaxStack) {
        ++target.count;
    } else {
        return; // A different block, or a full stack. Neither takes one more.
    }

    if (--m_cursor.count == 0) {
        m_cursor.clear();
    }
}

ItemStack Inventory::releaseCursor() {
    if (m_cursor.empty()) {
        return ItemStack{};
    }

    const BlockId block = m_cursor.block;
    const u32 leftover = add(block, m_cursor.count);
    m_cursor.clear();

    // Whatever the inventory had no room for. The caller drops it, because the
    // alternative -- deleting it because a window closed -- is the kind of loss a
    // player notices and cannot explain.
    return leftover > 0 ? ItemStack{block, leftover} : ItemStack{};
}

} // namespace mc
