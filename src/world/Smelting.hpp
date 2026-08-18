#pragma once

#include "core/Types.hpp"
#include "world/ItemTable.hpp"

#include <array>
#include <string_view>

namespace mc {

/// What a furnace turns things into, and what it burns to do it.
///
/// **Two tables, because they answer different questions about the same slot.** What
/// an item smelts *into* is a property of the ingredient; how long it *burns* is a
/// property of the fuel. A few things are both -- a log burns and nothing smelts it --
/// and collapsing them into one table would mean every entry carrying a field the
/// other half ignores.
///
/// Resolved at compile time exactly as `kRecipes` is, so a typo is a compile error
/// rather than a recipe that silently never fires. That property is the whole reason
/// `BlockTable` is shaped the way it is, and it costs nothing to keep here.

/// One smelting recipe: an ingredient, and what comes out.
///
/// No count, unlike a crafting recipe. **Vanilla smelting is always one for one**, and
/// a count field would be a column of 1s inviting someone to change one of them.
struct SmeltRecipe {
    ItemId input = kNoItem;
    ItemId output = kNoItem;
};

consteval SmeltRecipe smelts(std::string_view from, std::string_view to) {
    return SmeltRecipe{itemIdOf(from), itemIdOf(to)};
}

/// Every smelting recipe.
///
/// **The three ores that need it are the point of the phase.** Iron, copper and gold
/// are mineable already and were worth nothing, because the ore block is not the metal.
/// Cobblestone back to stone and sand to glass are vanilla's other early ones; glass is
/// not a block here yet, so sand is left out rather than smelted into nothing.
inline constexpr std::array kSmeltRecipes{
    smelts("iron_ore", "iron_ingot"),
    smelts("deepslate_iron_ore", "iron_ingot"),
    smelts("copper_ore", "copper_ingot"),
    smelts("deepslate_copper_ore", "copper_ingot"),
    smelts("gold_ore", "gold_ingot"),
    smelts("deepslate_gold_ore", "gold_ingot"),

    // Cobblestone back into stone. Vanilla's, and the one recipe here a player can
    // reach on their first furnace without finding an ore at all -- which makes it the
    // cheapest way to see that the thing works.
    smelts("cobblestone", "stone"),
};

/// One fuel: an item, and how many **ticks** it burns for.
///
/// Ticks rather than seconds because the furnace runs on the 20 Hz simulation tick,
/// the same clock `BlockUpdates` and falling sand use. Vanilla's numbers, which are
/// all multiples of the 200-tick smelt so they divide into a whole number of items.
struct FuelInfo {
    ItemId item = kNoItem;
    u32 ticks = 0;
};

consteval FuelInfo burns(std::string_view name, u32 ticks) {
    return FuelInfo{itemIdOf(name), ticks};
}

/// How long one smelt takes, in ticks. Vanilla's 200, which is ten seconds.
inline constexpr u32 kSmeltTicks = 200;

/// Everything that burns, with vanilla's durations.
///
/// **Coal is eight items and a plank is one and a half**, which is the ratio that
/// makes coal worth mining rather than a curiosity. A player who has not found coal
/// can still smelt with wood, slowly, which is exactly the pressure vanilla applies.
inline constexpr std::array kFuels{
    burns("coal", 1600),
    burns("oak_log", 300),
    burns("oak_planks", 300),
    burns("crafting_table", 300),
    burns("stick", 100),
};

/// What `input` smelts into, or `kNoItem` when nothing does.
constexpr ItemId smeltOutput(ItemId input) {
    if (input == kNoItem) {
        return kNoItem;
    }
    for (const SmeltRecipe& recipe : kSmeltRecipes) {
        if (recipe.input == input) {
            return recipe.output;
        }
    }
    return kNoItem;
}

/// How many ticks one of `item` burns for. Zero means it is not a fuel.
constexpr u32 burnTicks(ItemId item) {
    if (item == kNoItem) {
        return 0;
    }
    for (const FuelInfo& fuel : kFuels) {
        if (fuel.item == item) {
            return fuel.ticks;
        }
    }
    return 0;
}

constexpr bool isFuel(ItemId item) { return burnTicks(item) > 0; }
constexpr bool isSmeltable(ItemId item) { return smeltOutput(item) != kNoItem; }

} // namespace mc
