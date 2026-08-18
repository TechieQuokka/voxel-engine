#include "render/ScreenLayout.hpp"

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
/// a machine -- and the eye should not read the craft grid as more rows.
constexpr f32 kCraftSeparation = 0.055f;
/// Width of the arrow between the crafting grid and its output, as a fraction of a
/// slot. The gap it sits in is what says the output is produced by the grid rather
/// than being one more cell of it.
constexpr f32 kArrowWidth = 0.9f;
/// Border between the outermost slots and the panel edge.
constexpr f32 kPanelPadding = 0.028f;

/// Distance from the bottom edge to the bottom of the closed hotbar. Unchanged from
/// the value the HUD used before there was a window, so opening and closing a screen
/// does not move the hotbar the player was already looking at.
constexpr f32 kClosedBottomMargin = 0.04f;
constexpr f32 kClosedSlotHeight = 0.11f;
constexpr f32 kClosedGap = 0.012f;

usize craftEdgeOf(ScreenKind kind) {
    switch (kind) {
    case ScreenKind::Player:
        return 2;
    case ScreenKind::CraftingTable:
        return 3;
    }
    return 2;
}

} // namespace

ScreenLayout::ScreenLayout(f32 aspect, ScreenKind kind)
    : m_aspect(std::max(0.1f, aspect)), m_slotSize(kSlotSize), m_gap(kGap),
      m_craftEdge(craftEdgeOf(kind)) {
    m_containerSlots = m_craftEdge * m_craftEdge + 1;

    const f32 slotX = m_slotSize / m_aspect;
    const f32 gapX = m_gap / m_aspect;

    const f32 gridWidth = static_cast<f32>(kColumns) * slotX
                        + static_cast<f32>(kColumns - 1) * gapX;
    const f32 gridHeight = static_cast<f32>(kMainRows) * m_slotSize
                         + static_cast<f32>(kMainRows - 1) * m_gap;

    const auto craftEdge = static_cast<f32>(m_craftEdge);
    const f32 craftHeight = craftEdge * m_slotSize + (craftEdge - 1.0f) * m_gap;

    const f32 totalHeight = craftHeight + kCraftSeparation + gridHeight
                          + kHotbarSeparation + m_slotSize;

    // Centred on the screen. The window is the only thing on screen while it is
    // open, so there is nothing to sit beside. **A 2x2 window is shorter than a 3x3
    // one and both are centred**, which is why the player's slots are not at a fixed
    // height -- they move with the panel rather than the panel growing upward.
    m_gridLeft = -gridWidth * 0.5f;
    const f32 top = totalHeight * 0.5f;

    m_craftTop = top;
    m_gridTop = top - craftHeight - kCraftSeparation;
    m_hotbarY = m_gridTop - gridHeight - kHotbarSeparation - m_slotSize;

    // The craft cluster is right-aligned with the main grid, so the output slot sits
    // under the grid's right edge and the grid backs away from it. Vanilla's
    // arrangement, and it leaves the wide empty area on the left where vanilla draws
    // the player model -- which this engine has and does not draw here.
    const f32 arrowX = kArrowWidth * m_slotSize / m_aspect;
    const f32 outputRight = m_gridLeft + gridWidth;
    const f32 craftRight = outputRight - slotX - arrowX;
    m_craftLeft = craftRight - (craftEdge * slotX + (craftEdge - 1.0f) * gapX);

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

UiRect ScreenLayout::containerSlot(usize index) const {
    const usize cells = m_craftEdge * m_craftEdge;
    if (index == cells) {
        return m_output;
    }
    if (index > cells) {
        return UiRect{};
    }

    const f32 slotX = m_slotSize / m_aspect;
    const f32 gapX = m_gap / m_aspect;

    const usize row = index / m_craftEdge;
    const usize column = index % m_craftEdge;

    const f32 x = m_craftLeft + static_cast<f32>(column) * (slotX + gapX);
    const f32 y = m_craftTop - static_cast<f32>(row + 1) * m_slotSize
                - static_cast<f32>(row) * m_gap;
    return UiRect{x, y, x + slotX, y + m_slotSize};
}

UiRect ScreenLayout::playerSlot(usize index) const {
    const f32 slotX = m_slotSize / m_aspect;
    const f32 gapX = m_gap / m_aspect;

    if (index < Inventory::kHotbarSlots) {
        const f32 x = m_gridLeft + static_cast<f32>(index) * (slotX + gapX);
        return UiRect{x, m_hotbarY, x + slotX, m_hotbarY + m_slotSize};
    }

    if (index >= Inventory::kStorageSlots) {
        return UiRect{};
    }

    const usize main = index - Inventory::kHotbarSlots;
    const usize row = main / kColumns;
    const usize column = main % kColumns;

    // Row 0 at the top, so player slot 9 is the top-left of the grid. Vanilla's
    // order, and the one that makes "the row above the hotbar" mean the last row
    // rather than the first.
    const f32 x = m_gridLeft + static_cast<f32>(column) * (slotX + gapX);
    const f32 y = m_gridTop - static_cast<f32>(row + 1) * m_slotSize
                - static_cast<f32>(row) * m_gap;
    return UiRect{x, y, x + slotX, y + m_slotSize};
}

UiRect ScreenLayout::slot(usize index) const {
    if (index < m_containerSlots) {
        return containerSlot(index);
    }
    return playerSlot(index - m_containerSlots);
}

std::optional<usize> ScreenLayout::hitTest(f32 x, f32 y) const {
    for (usize i = 0; i < slotCount(); ++i) {
        if (slot(i).contains(x, y)) {
            return i;
        }
    }
    return std::nullopt;
}

UiRect ScreenLayout::closedHotbarSlot(usize index) const {
    const f32 slotX = kClosedSlotHeight / m_aspect;
    const f32 gapX = kClosedGap / m_aspect;
    const f32 total = static_cast<f32>(Inventory::kHotbarSlots) * slotX
                    + static_cast<f32>(Inventory::kHotbarSlots - 1) * gapX;

    const f32 x = -total * 0.5f + static_cast<f32>(index) * (slotX + gapX);
    const f32 y = -1.0f + kClosedBottomMargin;
    return UiRect{x, y, x + slotX, y + kClosedSlotHeight};
}

} // namespace mc
