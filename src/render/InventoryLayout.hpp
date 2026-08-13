#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Inventory.hpp"

#include <optional>

namespace mc {

/// A rectangle in normalised device coordinates.
struct UiRect {
    f32 x0 = 0.0f;
    f32 y0 = 0.0f;
    f32 x1 = 0.0f;
    f32 y1 = 0.0f;

    bool contains(f32 x, f32 y) const noexcept {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }
};

/// Where every slot is, on screen.
///
/// **One class, used by both the renderer and the hit test, and that is the whole
/// point of it existing.** If drawing computed slot rectangles and clicking computed
/// them again, the two would agree until someone adjusted a margin, and then clicks
/// would land a few pixels off the icon they appear to be on -- which reads as
/// unresponsive rather than as misaligned, and is miserable to track down. There is
/// one function, and the pixels and the pointer cannot disagree.
///
/// Screen space is NDC with y up, x scaled by aspect so slots come out square on a
/// window that is not.
class InventoryLayout {
public:
    explicit InventoryLayout(f32 aspect);

    /// Slot rectangles, indexed exactly as `Inventory` indexes its slots: 0-8 the
    /// hotbar, 9-35 the main grid, 36-44 the 3x3 crafting grid, 45 the output.
    /// **The hotbar is drawn as the bottom row of the window**, below the grid and
    /// separated by a gap, which is where vanilla puts it and is why a player can
    /// drag between the two without thinking about it.
    ///
    /// **One function for all forty-six, which is the point of this class.** The
    /// crafting grid needed no new hit-testing code at all: it is slots 36-44, and
    /// `hitTest` already walks every slot there is.
    UiRect slot(usize index) const;

    /// The arrow between the crafting grid and its output. Decoration, and the only
    /// rectangle here that is not a slot -- it is here rather than in the renderer
    /// because it has to sit in the gap this class chose, and a renderer that
    /// computed it separately would drift the moment a margin changed.
    UiRect craftArrow() const noexcept { return m_craftArrow; }

    /// The panel behind everything, for the backing plate and for "did this click
    /// land inside the window or outside it".
    UiRect panel() const noexcept { return m_panel; }

    /// Which slot contains this point, if any.
    std::optional<usize> hitTest(f32 x, f32 y) const;

    /// The hotbar as it is drawn when the window is *closed* -- centred along the
    /// bottom of the screen rather than inside a panel. A separate function because
    /// it is a different position, not a different size.
    UiRect closedHotbarSlot(usize index) const;

    static constexpr usize kColumns = 9;
    static constexpr usize kMainRows = Inventory::kMainSlots / kColumns;
    /// The crafting grid is square, so one number covers rows and columns.
    static constexpr usize kCraftColumns = 3;

private:
    f32 m_aspect = 1.0f;
    /// Slot edge length in NDC y units. x is divided by the aspect at use.
    f32 m_slotSize = 0.0f;
    f32 m_gap = 0.0f;
    /// Bottom-left of the main grid's first (top-left) slot row.
    f32 m_gridLeft = 0.0f;
    f32 m_gridTop = 0.0f;
    f32 m_hotbarY = 0.0f;
    /// Top-left of the crafting grid, which sits above the main grid and is
    /// right-aligned with it -- vanilla's arrangement.
    f32 m_craftLeft = 0.0f;
    f32 m_craftTop = 0.0f;
    UiRect m_output{};
    UiRect m_craftArrow{};
    UiRect m_panel{};
};

} // namespace mc
