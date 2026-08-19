#pragma once

#include "core/Types.hpp"
#include "world/Container.hpp"
#include "world/Smelting.hpp"

#include <array>

namespace mc {

/// One furnace: an ingredient, a fuel, an output, and the two timers between them.
///
/// **The first container with a life of its own.** A crafting grid does nothing
/// between clicks and is thrown away when its window closes; a furnace burns whether
/// or not anyone is looking at it, so it has to exist somewhere that outlives the
/// screen and be ticked by the simulation rather than by the UI. `Engine` keeps them
/// in a map keyed by block position for exactly that reason.
///
/// **The output slot is take-only and takes no input**, which is what `SlotKind` was
/// for: a player must not be able to put cobblestone in the output and have it count
/// as smelted. It is the same slot kind a crafting result uses, and it needed no new
/// interaction code.
class Furnace final : public Container {
public:
    /// Slot indices, in the order they are drawn: ingredient on top, fuel below it,
    /// output to the right. Vanilla's arrangement.
    static constexpr usize kInputSlot = 0;
    static constexpr usize kFuelSlot = 1;
    static constexpr usize kOutputSlot = 2;
    static constexpr usize kSlots = 3;

    /// Advances by `ticks` simulation ticks. Called on the 20 Hz clock, for every
    /// furnace that exists, whether or not its window is open.
    ///
    /// Returns whether anything changed, so a caller can avoid redrawing or, later,
    /// avoid writing a save file for a furnace that sat idle.
    bool tick(u32 ticks);

    /// Whether it is currently burning, for the flame the window draws.
    bool burning() const noexcept { return m_burnRemaining > 0; }

    /// Cook progress in [0, 1], for the arrow the window draws.
    f32 cookProgress() const noexcept {
        return static_cast<f32>(m_cookTicks) / static_cast<f32>(kSmeltTicks);
    }

    /// Fuel left as a fraction of what the current fuel item gave, in [0, 1].
    f32 burnProgress() const noexcept {
        return m_burnTotal == 0 ? 0.0f
                                : static_cast<f32>(m_burnRemaining)
                                    / static_cast<f32>(m_burnTotal);
    }

    /// Whether every slot is empty, so `Engine` can forget a furnace nobody used
    /// rather than keeping one per block ever right-clicked.
    bool idle() const noexcept;

    /// The two timers, for persistence.
    ///
    /// **A furnace that forgot what it was smelting when the player walked away was
    /// the defect that made Phase 11 worth doing before Phase 19.** The slots go to
    /// disk through `at()`, which already exists; the timers had nowhere to be read
    /// from, and a furnace restored with full slots and a zeroed burn timer would
    /// silently relight from its own fuel and lose a smelt.
    struct Timers {
        u32 burnRemaining = 0;
        u32 burnTotal = 0;
        u32 cookTicks = 0;
    };

    Timers timers() const noexcept { return {m_burnRemaining, m_burnTotal, m_cookTicks}; }

    /// Puts the timers back. The slots are restored through `mutableAt()`.
    ///
    /// Clamps rather than trusting: `cookProgress` and `burnProgress` divide by
    /// their totals to drive the two gauges, and a saved file could name a cook
    /// count past `kSmeltTicks` or a remainder larger than the total, either of
    /// which draws a bar past the end of its frame.
    void restoreTimers(const Timers& timers) noexcept;

    // -- Container ------------------------------------------------------------

    usize slotCount() const override { return kSlots; }
    SlotKind kindOf(usize slot) const override;
    const ItemStack& at(usize slot) const override;
    ItemStack& mutableAt(usize slot) override;
    void takeOutput(usize slot, ItemStack& cursor) override;
    ItemStack releaseOne() override;

private:
    /// Whether the ingredient smelts into something the output slot can accept.
    bool canSmelt() const;
    /// Consumes one fuel item if there is one and nothing is currently burning.
    bool lightFuel();

    std::array<ItemStack, kSlots> m_slots{};

    /// Ticks of fuel left, and what the current fuel item was worth. The second is
    /// only for the flame gauge -- a bar that shrinks needs to know what full was.
    u32 m_burnRemaining = 0;
    u32 m_burnTotal = 0;

    /// Progress into the current smelt, in ticks, out of `kSmeltTicks`.
    u32 m_cookTicks = 0;
};

} // namespace mc
