#pragma once

#include "core/Types.hpp"
#include "world/Container.hpp"
#include "world/Crafting.hpp"

#include <array>

namespace mc {

/// A square crafting grid and the result it currently produces.
///
/// **The edge length is the entire difference between the player's own grid and a
/// crafting table**, and making it a parameter rather than two classes is what makes
/// vanilla's gate work: 2x2 in the player's window, 3x3 at a table, one implementation
/// and one recipe matcher. A pickaxe is three planks across the top and two sticks
/// down the middle, so it simply does not fit in four cells -- and *that* is the
/// table's reason to exist, expressed as arithmetic rather than as a rule.
///
/// Phase 16 shipped 3x3 in the player's window because a bench was a second window and
/// there was no way to have one. `Container` is that way, so the grid goes back to
/// vanilla's shape and the player loses something -- which is exactly what vanilla
/// does and is why the table feels like a step forward rather than a formality.
class CraftingGrid final : public Container {
public:
    /// Vanilla's largest grid, and what a recipe is written against.
    static constexpr usize kMaxEdge = 3;
    static constexpr usize kMaxCells = kMaxEdge * kMaxEdge;

    /// `edge` is 2 for the player's own grid and 3 for a crafting table. Anything
    /// larger is clamped, because a recipe table written for 3x3 cannot describe it.
    explicit CraftingGrid(usize edge);

    usize edge() const noexcept { return m_edge; }
    /// Input cells only. The output is one past them.
    usize cellCount() const noexcept { return m_edge * m_edge; }
    /// Index of the output slot within this container.
    usize outputSlot() const noexcept { return cellCount(); }

    /// What the grid would make right now, or an empty result.
    CraftResult result() const;

    // -- Container ------------------------------------------------------------

    usize slotCount() const override { return cellCount() + 1; }
    SlotKind kindOf(usize slot) const override;
    const ItemStack& at(usize slot) const override;
    ItemStack& mutableAt(usize slot) override;
    void takeOutput(usize slot, ItemStack& cursor) override;
    ItemStack releaseOne() override;

private:
    /// The grid laid into the top-left of a 3x3, which is what `matchRecipe` reads.
    ///
    /// **Shaped recipes match anywhere in the 3x3 and match mirrored**, so a 2x2 grid
    /// placed in a corner matches every recipe small enough to fit and no recipe that
    /// is not. There is no second matcher and no per-size recipe table; the padding is
    /// the whole mechanism.
    std::array<ItemId, kMaxCells> paddedIds() const;

    usize m_edge = 2;
    std::array<ItemStack, kMaxCells> m_cells{};

    /// Returned by `at` for the output, which is computed rather than stored. Kept as
    /// a member so a reference to it outlives the call.
    mutable ItemStack m_outputView{};
};

} // namespace mc
