#include "world/Screen.hpp"

#include <algorithm>

namespace mc {
namespace {

const ItemStack kNothing{};

} // namespace

const ItemStack& Screen::at(usize slot) const {
    if (slot < containerSlots()) {
        return m_container->at(slot);
    }
    const usize player = slot - containerSlots();
    return player < Inventory::kStorageSlots ? m_inventory->at(player) : kNothing;
}

SlotKind Screen::kindOf(usize slot) const {
    return slot < containerSlots() ? m_container->kindOf(slot) : SlotKind::Normal;
}

ItemStack* Screen::mutableAt(usize slot) {
    if (slot < containerSlots()) {
        return m_container->kindOf(slot) == SlotKind::Output
                 ? nullptr
                 : &m_container->mutableAt(slot);
    }
    const usize player = slot - containerSlots();
    return player < Inventory::kStorageSlots ? &m_inventory->mutableAt(player) : nullptr;
}

void Screen::click(usize slot) {
    if (slot >= slotCount()) {
        return;
    }

    ItemStack& cursor = m_inventory->mutableCursor();

    if (kindOf(slot) == SlotKind::Output) {
        m_container->takeOutput(slot, cursor);
        return;
    }

    ItemStack* target = mutableAt(slot);
    if (target == nullptr) {
        return;
    }

    if (cursor.empty()) {
        // Nothing in hand: pick the whole stack up. An empty slot leaves the hand
        // empty, which is a click that does nothing rather than one that errors.
        cursor = *target;
        target->clear();
        return;
    }

    if (target->empty()) {
        *target = cursor;
        cursor.clear();
        return;
    }

    if (target->item == cursor.item) {
        // Same type: pour as much as fits and keep the rest in hand. Keeping the
        // remainder rather than refusing is what lets a player top a stack up from a
        // bigger one without first finding somewhere to put the difference.
        const u32 limit = maxStackOf(target->item);
        const u32 room = target->count < limit ? limit - target->count : 0u;
        const u32 moved = std::min(room, cursor.count);
        target->count += moved;
        cursor.count -= moved;
        if (cursor.count == 0) {
            cursor.clear();
        }
        return;
    }

    std::swap(*target, cursor);
}

void Screen::split(usize slot) {
    if (slot >= slotCount()) {
        return;
    }

    ItemStack& cursor = m_inventory->mutableCursor();

    if (kindOf(slot) == SlotKind::Output) {
        // There is no half of a crafted result to take. Vanilla crafts one here too.
        m_container->takeOutput(slot, cursor);
        return;
    }

    ItemStack* target = mutableAt(slot);
    if (target == nullptr) {
        return;
    }

    if (cursor.empty()) {
        if (target->empty()) {
            return;
        }
        // The larger half stays in the hand, which is vanilla's rounding and is the
        // one people expect when splitting an odd stack.
        const u32 taken = (target->count + 1) / 2;
        cursor.item = target->item;
        cursor.count = taken;
        target->count -= taken;
        if (target->count == 0) {
            target->clear();
        }
        return;
    }

    // Something in hand: put down exactly one, which is how a stack gets spread
    // across slots one at a time. **This is the click that fills a crafting grid**,
    // and it is why the grid needed no interaction of its own.
    if (target->empty()) {
        target->item = cursor.item;
        target->count = 1;
    } else if (target->item == cursor.item && target->count < maxStackOf(target->item)) {
        ++target->count;
    } else {
        return; // A different item, or a full stack. Neither takes one more.
    }

    if (--cursor.count == 0) {
        cursor.clear();
    }
}

Screen::Release Screen::releaseOne() {
    // The hand first. It is the stack the player is most obviously holding, and
    // emptying it before the container means a grid cell can land back in the slot
    // the cursor came from.
    //
    // `cursorEmpty` is asked before `releaseCursor` rather than inferred from what it
    // returns, for the reason `Release` exists at all: a cursor that went back into
    // storage cleanly returns the same empty stack as no cursor at all.
    if (!m_inventory->cursorEmpty()) {
        return Release{true, m_inventory->releaseCursor()};
    }

    const ItemStack given = m_container->releaseOne();
    if (given.empty()) {
        return Release{}; // Nothing left. This is the only false.
    }

    const u32 leftover = m_inventory->add(given.item, given.count);
    return Release{true, leftover > 0 ? ItemStack{given.item, leftover} : ItemStack{}};
}

} // namespace mc
