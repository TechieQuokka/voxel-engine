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

/// Which window is open, and therefore what sits above the player's own slots.
///
/// **An enum in the render layer rather than a layout description coming out of
/// `world`.** A container knows how many slots it has and what they do; where they
/// are drawn is a question about screens, and answering it in `world` would put
/// pixel geometry in the module that is meant to know nothing about rendering.
/// Adding a furnace is an entry here and a case in one function.
enum class ScreenKind : u8 {
    /// The player's own window: a 2x2 crafting grid and its output.
    Player,
    /// A crafting table: 3x3 and its output. The only difference is the edge, which
    /// is the only difference vanilla has either.
    CraftingTable,
    /// A furnace: one column of two -- ingredient over fuel -- and its output.
    Furnace,
};

/// Where every slot of the open screen is, on screen.
///
/// **One class, used by both the renderer and the hit test, and that is the whole
/// point of it existing.** If drawing computed slot rectangles and clicking computed
/// them again, the two would agree until someone adjusted a margin, and then clicks
/// would land a few pixels off the icon they appear to be on -- which reads as
/// unresponsive rather than as misaligned, and is miserable to track down. There is
/// one function, and the pixels and the pointer cannot disagree.
///
/// It was `InventoryLayout` and knew one window. The generalisation is small on
/// purpose: the player's thirty-six slots are laid out identically in every screen
/// there will ever be, so what varies is only the block above them.
///
/// Screen space is NDC with y up, x scaled by aspect so slots come out square on a
/// window that is not.
class ScreenLayout {
public:
    ScreenLayout(f32 aspect, ScreenKind kind);

    /// Slot rectangles, indexed exactly as `Screen` indexes its slots: the
    /// container's own slots first, then the player's thirty-six with the hotbar at
    /// 0-8.
    ///
    /// **The hotbar is drawn as the bottom row of the window**, below the main grid
    /// and separated by a gap, which is where vanilla puts it and is why a player can
    /// drag between the two without thinking about it.
    UiRect slot(usize index) const;

    usize containerSlots() const noexcept { return m_containerSlots; }
    usize slotCount() const noexcept { return m_containerSlots + Inventory::kStorageSlots; }

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

    /// The hotbar as it is drawn when no window is open -- centred along the bottom
    /// of the screen rather than inside a panel. A separate function because it is a
    /// different position, not a different size.
    UiRect closedHotbarSlot(usize index) const;

    static constexpr usize kColumns = 9;
    static constexpr usize kMainRows = Inventory::kMainSlots / kColumns;

private:
    /// A container slot: a cell of the craft grid, or its output.
    UiRect containerSlot(usize index) const;
    /// One of the player's thirty-six, indexed as `Inventory` indexes them.
    UiRect playerSlot(usize index) const;

    f32 m_aspect = 1.0f;
    /// Slot edge length in NDC y units. x is divided by the aspect at use.
    f32 m_slotSize = 0.0f;
    f32 m_gap = 0.0f;

    /// The container's cells as a rectangle, which is all three windows have in
    /// common: 2x2 for the player, 3x3 for a table, 1 wide and 2 tall for a furnace.
    /// **Generalising the crafting edge to rows and columns is what made the furnace
    /// need no layout code of its own.**
    usize m_containerRows = 2;
    usize m_containerColumns = 2;
    usize m_containerSlots = 5;

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
