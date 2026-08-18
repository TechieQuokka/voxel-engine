#include "world/CraftingGrid.hpp"

#include <algorithm>

namespace mc {
namespace {

const ItemStack kNothing{};

} // namespace

CraftingGrid::CraftingGrid(usize edge) : m_edge(std::clamp<usize>(edge, 1, kMaxEdge)) {}

std::array<ItemId, CraftingGrid::kMaxCells> CraftingGrid::paddedIds() const {
    std::array<ItemId, kMaxCells> ids{};
    ids.fill(kNoItem);

    // Row-major into the top-left corner. `matchRecipe` slides a shaped pattern
    // across the 3x3, so where the corner is does not matter -- only that the cells
    // keep their relative positions, which is what a pattern *is*.
    for (usize row = 0; row < m_edge; ++row) {
        for (usize column = 0; column < m_edge; ++column) {
            const ItemStack& cell = m_cells[row * m_edge + column];
            ids[row * kMaxEdge + column] = cell.empty() ? kNoItem : cell.item;
        }
    }
    return ids;
}

CraftResult CraftingGrid::result() const {
    return matchRecipe(paddedIds());
}

SlotKind CraftingGrid::kindOf(usize slot) const {
    return slot == outputSlot() ? SlotKind::Output : SlotKind::Normal;
}

const ItemStack& CraftingGrid::at(usize slot) const {
    if (slot == outputSlot()) {
        // Computed on demand rather than kept in step by every mutation. There is no
        // state to get wrong this way, and the grid is nine cells against a recipe
        // table of ten -- the cost of asking is not worth caching.
        const CraftResult result = this->result();
        m_outputView.item = result.item;
        m_outputView.count = result.count;
        return m_outputView;
    }
    return slot < cellCount() ? m_cells[slot] : kNothing;
}

ItemStack& CraftingGrid::mutableAt(usize slot) {
    // The output is never reached through here; `Screen` routes it to `takeOutput`.
    // Returning cell 0 for an out-of-range slot would silently edit the grid, so the
    // assert is the honest answer and the clamp below is only for release builds.
    return m_cells[std::min(slot, cellCount() - 1)];
}

void CraftingGrid::takeOutput(usize slot, ItemStack& cursor) {
    if (slot != outputSlot()) {
        return;
    }

    const CraftResult result = this->result();
    if (result.empty()) {
        return;
    }

    // The hand has to be able to receive the whole output before anything is
    // consumed. Crafting into a hand with no room would either destroy the result or
    // leave the grid half-eaten, and both are the kind of loss a player cannot
    // explain.
    if (!cursor.empty()) {
        if (cursor.item != result.item) {
            return;
        }
        if (cursor.count + result.count > maxStackOf(result.item)) {
            return;
        }
    }

    for (usize i = 0; i < cellCount(); ++i) {
        ItemStack& cell = m_cells[i];
        if (cell.empty()) {
            continue;
        }
        // One from each occupied cell, whatever the stack depth. That is what lets a
        // grid of full stacks be clicked repeatedly rather than crafting once and
        // needing to be refilled.
        if (--cell.count == 0) {
            cell.clear();
        }
    }

    cursor.item = result.item;
    cursor.count += result.count;
}

ItemStack CraftingGrid::releaseOne() {
    for (usize i = 0; i < cellCount(); ++i) {
        ItemStack& cell = m_cells[i];
        if (cell.empty()) {
            continue;
        }
        const ItemStack taken = cell;
        cell.clear();
        return taken;
    }
    return ItemStack{};
}

} // namespace mc
