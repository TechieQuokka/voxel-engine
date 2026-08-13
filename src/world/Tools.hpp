#pragma once

#include "core/Types.hpp"

namespace mc {

/// What a tool is for, and what a block wants swung at it.
///
/// A separate header from both tables because **both of them need it and neither may
/// include the other**. `BlockTable` names the tool a block responds to; `ItemTable`
/// names the tool an item *is*, and it already includes `BlockTable` because item ids
/// extend block ids. Putting these two enums in either table would close that loop.
///
/// `Sword` is here and is deliberately inert: it multiplies nothing and harvests
/// nothing, because until Phase 19 there is nothing in the world to swing it at. It
/// exists so that the recipe can, and so the day mobs land it is already the thing
/// they are fought with.
enum class ToolKind : u8 {
    None,
    Pickaxe,
    Axe,
    Shovel,
    Sword,
};

/// Tool material. **`Iron` and `Diamond` have no recipes yet and that is not an
/// oversight** -- both need an ingot, an ingot needs smelting, and a furnace is a
/// second window, which is Phase 17. They are named here because block harvest
/// requirements are vanilla's and vanilla's already reference them: iron ore needs
/// stone, and diamond ore needs iron. Naming the tier a block requires is how the
/// engine says "not yet" rather than silently making diamond hand-mineable.
enum class ToolTier : u8 {
    None,
    Wood,
    Stone,
    Iron,
    Diamond,
};

/// Vanilla's mining-speed multiplier, applied only when the tool matches the block.
///
/// The numbers are the game's own: hand 1, wood 2, stone 4, iron 6, diamond 8. They
/// enter `breakSeconds` as a divisor, so a stone pickaxe is four times faster on
/// stone and exactly as slow as a hand on dirt.
constexpr f32 tierSpeed(ToolTier tier) {
    switch (tier) {
    case ToolTier::None:    return 1.0f;
    case ToolTier::Wood:    return 2.0f;
    case ToolTier::Stone:   return 4.0f;
    case ToolTier::Iron:    return 6.0f;
    case ToolTier::Diamond: return 8.0f;
    }
    return 1.0f;
}

/// Ordering, for "is this tool at least as good as the block demands".
///
/// A rank rather than comparing the enum directly: the enumerators happen to be in
/// order today, and a comparison that relies on that would break silently the first
/// time someone inserts a tier in the middle -- gold, in vanilla, sits between stone
/// and iron by recipe and nowhere near it by speed.
constexpr u32 tierRank(ToolTier tier) {
    switch (tier) {
    case ToolTier::None:    return 0;
    case ToolTier::Wood:    return 1;
    case ToolTier::Stone:   return 2;
    case ToolTier::Iron:    return 3;
    case ToolTier::Diamond: return 4;
    }
    return 0;
}

} // namespace mc
