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
/// Space between the crafting area and the main grid. Wider than the hotbar's gap,
/// because these two are doing genuinely different jobs -- one is storage and one is
/// a machine -- and the eye should not read the craft grid as three more rows.
constexpr f32 kCraftSeparation = 0.055f;
/// Width of the arrow between the crafting grid and its output, as a fraction of a
/// slot. The gap it sits in is what says the output is produced by the grid rather
/// than being a tenth cell of it.
constexpr f32 kArrowWidth = 0.9f;
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

    const f32 craftHeight = static_cast<f32>(kCraftColumns) * m_slotSize
                          + static_cast<f32>(kCraftColumns - 1) * m_gap;

    const f32 totalHeight = craftHeight + kCraftSeparation + gridHeight
                          + kHotbarSeparation + m_slotSize;

    // Centred on the screen. The window is the only thing on screen while it is
    // open, so there is nothing to sit beside.
    m_gridLeft = -gridWidth * 0.5f;
    const f32 top = totalHeight * 0.5f;

    m_craftTop = top;
    m_gridTop = top - craftHeight - kCraftSeparation;
    m_hotbarY = m_gridTop - gridHeight - kHotbarSeparation - m_slotSize;

    // The craft cluster is right-aligned with the main grid, so the output slot sits
    // under the grid's right edge and the 3x3 backs away from it. Vanilla's
    // arrangement, and it leaves the wide empty area on the left where vanilla draws
    // the player model -- which this engine has and does not draw here.
    const f32 arrowX = kArrowWidth * m_slotSize / m_aspect;
    const f32 outputRight = m_gridLeft + gridWidth;
    const f32 craftRight = outputRight - slotX - arrowX;
    m_craftLeft = craftRight - (static_cast<f32>(kCraftColumns) * slotX
                                + static_cast<f32>(kCraftColumns - 1) * gapX);

    const f32 craftMiddle = m_craftTop - craftHeight * 0.5f;
    m_output = UiRect{outputRight - slotX, craftMiddle - m_slotSize * 0.5f,
                      outputRight, craftMiddle + m_slotSize * 0.5f};

    // Thinner than the slots it sits between, and vertically centred on them.
    const f32 arrowHeight = m_slotSize * 0.22f;
    m_craftArrow = UiRect{craftRight + arrowX * 0.15f, craftMiddle - arrowHeight * 0.5f,
                          outputRight - slotX - arrowX * 0.15f,
                          craftMiddle + arrowHeight * 0.5f};

    const f32 padX = kPanelPadding / m_aspect;
    m_panel = UiRect{m_gridLeft - padX, m_hotbarY - kPanelPadding,
                     m_gridLeft + gridWidth + padX, top + kPanelPadding};
}

UiRect InventoryLayout::slot(usize index) const {
    const f32 slotX = m_slotSize / m_aspect;
    const f32 gapX = m_gap / m_aspect;

    if (index < Inventory::kHotbarSlots) {
        const f32 x = m_gridLeft + static_cast<f32>(index) * (slotX + gapX);
        return UiRect{x, m_hotbarY, x + slotX, m_hotbarY + m_slotSize};
    }

    if (index == Inventory::kOutputSlot) {
        return m_output;
    }

    if (index >= Inventory::kFirstCraftSlot) {
        if (index >= Inventory::kSlotCount) {
            return UiRect{};
        }
        const usize craft = index - Inventory::kFirstCraftSlot;
        const usize craftRow = craft / kCraftColumns;
        const usize craftColumn = craft % kCraftColumns;

        const f32 cx = m_craftLeft + static_cast<f32>(craftColumn) * (slotX + gapX);
        const f32 cy = m_craftTop - static_cast<f32>(craftRow + 1) * m_slotSize
                     - static_cast<f32>(craftRow) * m_gap;
        return UiRect{cx, cy, cx + slotX, cy + m_slotSize};
    }

    if (index >= Inventory::kStorageSlots) {
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
