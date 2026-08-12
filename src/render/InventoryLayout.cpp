#include "render/InventoryLayout.hpp"

#include <algorithm>

namespace mc {
namespace {

/// Slot edge length as a fraction of screen height, and the gap between slots.
/// Chosen so nine slots plus their gaps span a little over half the width on a 16:9
/// window, which is roughly the proportion vanilla's window occupies.
constexpr f32 kSlotSize = 0.105f;
constexpr f32 kGap = 0.014f;
/// Space between the main grid and the hotbar row, which is what makes the hotbar
/// read as a separate thing rather than as a fourth row.
constexpr f32 kHotbarSeparation = 0.045f;
/// Border between the outermost slots and the panel edge.
constexpr f32 kPanelPadding = 0.028f;

/// Distance from the bottom edge to the bottom of the closed hotbar. Unchanged from
/// the value the HUD used before there was a window, so opening and closing the
/// inventory does not move the hotbar the player was already looking at.
constexpr f32 kClosedBottomMargin = 0.04f;
constexpr f32 kClosedSlotHeight = 0.11f;
constexpr f32 kClosedGap = 0.012f;

} // namespace

InventoryLayout::InventoryLayout(f32 aspect)
    : m_aspect(std::max(0.1f, aspect)), m_slotSize(kSlotSize), m_gap(kGap) {
    const f32 slotX = m_slotSize / m_aspect;
    const f32 gapX = m_gap / m_aspect;

    const f32 gridWidth = static_cast<f32>(kColumns) * slotX
                        + static_cast<f32>(kColumns - 1) * gapX;
    const f32 gridHeight = static_cast<f32>(kMainRows) * m_slotSize
                         + static_cast<f32>(kMainRows - 1) * m_gap;

    const f32 totalHeight = gridHeight + kHotbarSeparation + m_slotSize;

    // Centred on the screen. The window is the only thing on screen while it is
    // open, so there is nothing to sit beside.
    m_gridLeft = -gridWidth * 0.5f;
    m_gridTop = totalHeight * 0.5f;
    m_hotbarY = m_gridTop - gridHeight - kHotbarSeparation - m_slotSize;

    const f32 padX = kPanelPadding / m_aspect;
    m_panel = UiRect{m_gridLeft - padX, m_hotbarY - kPanelPadding,
                     m_gridLeft + gridWidth + padX, m_gridTop + kPanelPadding};
}

UiRect InventoryLayout::slot(usize index) const {
    const f32 slotX = m_slotSize / m_aspect;
    const f32 gapX = m_gap / m_aspect;

    if (index < Inventory::kHotbarSlots) {
        const f32 x = m_gridLeft + static_cast<f32>(index) * (slotX + gapX);
        return UiRect{x, m_hotbarY, x + slotX, m_hotbarY + m_slotSize};
    }

    if (index >= Inventory::kSlotCount) {
        return UiRect{};
    }

    const usize main = index - Inventory::kHotbarSlots;
    const usize row = main / kColumns;
    const usize column = main % kColumns;

    // Row 0 at the top, so slot 9 is the top-left of the grid. Vanilla's order, and
    // the one that makes "the row above the hotbar" mean the last row rather than
    // the first.
    const f32 x = m_gridLeft + static_cast<f32>(column) * (slotX + gapX);
    const f32 y = m_gridTop - static_cast<f32>(row + 1) * m_slotSize
                - static_cast<f32>(row) * m_gap;
    return UiRect{x, y, x + slotX, y + m_slotSize};
}

std::optional<usize> InventoryLayout::hitTest(f32 x, f32 y) const {
    for (usize i = 0; i < Inventory::kSlotCount; ++i) {
        if (slot(i).contains(x, y)) {
            return i;
        }
    }
    return std::nullopt;
}

UiRect InventoryLayout::closedHotbarSlot(usize index) const {
    const f32 slotX = kClosedSlotHeight / m_aspect;
    const f32 gapX = kClosedGap / m_aspect;
    const f32 total = static_cast<f32>(Inventory::kHotbarSlots) * slotX
                    + static_cast<f32>(Inventory::kHotbarSlots - 1) * gapX;

    const f32 x = -total * 0.5f + static_cast<f32>(index) * (slotX + gapX);
    const f32 y = -1.0f + kClosedBottomMargin;
    return UiRect{x, y, x + slotX, y + kClosedSlotHeight};
}

} // namespace mc
