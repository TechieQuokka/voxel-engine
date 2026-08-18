#include "world/Furnace.hpp"

#include <algorithm>

namespace mc {
namespace {

const ItemStack kNothing{};

} // namespace

SlotKind Furnace::kindOf(usize slot) const {
    return slot == kOutputSlot ? SlotKind::Output : SlotKind::Normal;
}

const ItemStack& Furnace::at(usize slot) const {
    return slot < kSlots ? m_slots[slot] : kNothing;
}

ItemStack& Furnace::mutableAt(usize slot) {
    // The output never reaches here; `Screen` routes it to `takeOutput`.
    return m_slots[std::min(slot, kSlots - 1)];
}

bool Furnace::idle() const noexcept {
    return m_slots[kInputSlot].empty() && m_slots[kFuelSlot].empty()
        && m_slots[kOutputSlot].empty() && m_burnRemaining == 0 && m_cookTicks == 0;
}

bool Furnace::canSmelt() const {
    const ItemId result = smeltOutput(m_slots[kInputSlot].item);
    if (m_slots[kInputSlot].empty() || result == kNoItem) {
        return false;
    }

    // The output has to be able to receive it *before* anything is consumed. A
    // furnace whose output is full stops rather than destroying what it made, which
    // is vanilla's behaviour and the only one a player can reason about.
    const ItemStack& output = m_slots[kOutputSlot];
    if (output.empty()) {
        return true;
    }
    return output.item == result && output.count < maxStackOf(result);
}

bool Furnace::lightFuel() {
    ItemStack& fuel = m_slots[kFuelSlot];
    const u32 ticks = burnTicks(fuel.item);
    if (fuel.empty() || ticks == 0) {
        return false;
    }

    m_burnRemaining = ticks;
    m_burnTotal = ticks;
    if (--fuel.count == 0) {
        fuel.clear();
    }
    return true;
}

bool Furnace::tick(u32 ticks) {
    bool changed = false;

    for (u32 i = 0; i < ticks; ++i) {
        // **Fuel is consumed to *start* burning, not to smelt.** That is why a
        // furnace given one coal and one ore keeps burning after the ore is done and
        // wastes the rest: vanilla does the same, and it is what makes filling the
        // input slot before lighting it the right move rather than a superstition.
        if (m_burnRemaining == 0 && canSmelt() && lightFuel()) {
            changed = true;
        }

        // **Cooking happens before the fuel is spent, and the order is the bug this
        // had.** Decrementing first means the tick that takes the fuel from one to
        // zero cooks nothing, so 1600 ticks of coal smelted seven items instead of
        // eight -- an off-by-one that looks like a balance decision rather than a
        // mistake, which is exactly why it needed a test with vanilla's number in it.
        const bool burningThisTick = m_burnRemaining > 0;

        if (burningThisTick && canSmelt()) {
            ++m_cookTicks;
            changed = true;

            if (m_cookTicks >= kSmeltTicks) {
                m_cookTicks = 0;

                ItemStack& input = m_slots[kInputSlot];
                const ItemId result = smeltOutput(input.item);
                if (--input.count == 0) {
                    input.clear();
                }

                ItemStack& output = m_slots[kOutputSlot];
                output.item = result;
                ++output.count;
            }
        } else if (m_cookTicks > 0) {
            // Progress decays rather than resetting, which is vanilla's rule: a
            // furnace that runs out of fuel mid-smelt loses ground slowly instead of
            // throwing the whole item away.
            m_cookTicks -= std::min(m_cookTicks, 2u);
            changed = true;
        }

        if (burningThisTick) {
            --m_burnRemaining;
            changed = true;
            if (m_burnRemaining == 0) {
                m_burnTotal = 0;
            }
        }
    }

    return changed;
}

void Furnace::takeOutput(usize slot, ItemStack& cursor) {
    if (slot != kOutputSlot) {
        return;
    }

    ItemStack& output = m_slots[kOutputSlot];
    if (output.empty()) {
        return;
    }

    // **The whole stack, not one**, and this is where a furnace differs from a
    // crafting grid. A crafting result is produced by the click; this one was
    // produced by the fire and is already sitting there, so taking it is an ordinary
    // pick-up that happens to be one-way.
    if (cursor.empty()) {
        cursor = output;
        output.clear();
        return;
    }

    if (cursor.item != output.item) {
        return;
    }

    const u32 limit = maxStackOf(cursor.item);
    const u32 room = cursor.count < limit ? limit - cursor.count : 0u;
    const u32 moved = std::min(room, output.count);
    cursor.count += moved;
    output.count -= moved;
    if (output.count == 0) {
        output.clear();
    }
}

ItemStack Furnace::releaseOne() {
    // **Nothing.** A furnace keeps what is in it when its window closes, which is the
    // difference between a machine you walk away from and a grid you were holding.
    // `Container::releaseOne` is virtual precisely for this: applying the crafting
    // grid's rule here would empty a furnace every time a player glanced at it.
    //
    // Breaking the block is what returns the contents, and `Engine` does that.
    return ItemStack{};
}

} // namespace mc
