#pragma once

#include "core/Types.hpp"
#include "world/ItemTable.hpp"

#include <array>
#include <string_view>

namespace mc {

/// The crafting grid and the recipes it matches.
///
/// **The grid is 3x3 and it is in the player's own window, which is not vanilla.**
/// Vanilla gives the player 2x2 and puts 3x3 behind a crafting bench, and that
/// restriction is the bench's entire reason to exist. It is not copied here because
/// **a pickaxe is a 3x3 recipe** -- three planks across the top and two sticks down
/// the middle -- so a 2x2 grid could not make the one thing Phase 16 is for, and a
/// bench is a second window, which is Phase 17's cost and not this one's.
///
/// Phase 17 moves 3x3 to the bench and cuts the player's grid to 2x2. That is a
/// *reduction* in what the player can do without walking to a block, and presenting
/// it as a feature is exactly what vanilla does.
///
/// Recipes are a table of ids resolved at compile time, so a typo in a recipe is a
/// compile error rather than a recipe that silently never matches -- the same
/// property `BlockTable` exists to keep.
struct Recipe {
    /// Row-major 3x3. `kNoItem` is an empty cell.
    std::array<ItemId, 9> grid{};
    ItemId output = kNoItem;
    u32 count = 0;

    /// Shapeless recipes match on *which* items are present and ignore where they
    /// sit. Vanilla uses them where arrangement would be arbitrary -- one log makes
    /// planks wherever the log is put, and demanding the top-left cell would be a
    /// rule the player has to learn for no reason.
    bool shapeless = false;
};

/// `kNoItem` for an empty cell, an id for anything else. A name that matches nothing
/// is still a compile error, because `itemIdOf` is consteval.
consteval ItemId cell(std::string_view name) {
    return name.empty() ? kNoItem : itemIdOf(name);
}

consteval Recipe shaped(ItemId output, u32 count,
                        std::string_view a, std::string_view b, std::string_view c,
                        std::string_view d, std::string_view e, std::string_view f,
                        std::string_view g, std::string_view h, std::string_view i) {
    return Recipe{{cell(a), cell(b), cell(c),
                   cell(d), cell(e), cell(f),
                   cell(g), cell(h), cell(i)},
                  output, count, false};
}

consteval Recipe shapeless1(ItemId output, u32 count, std::string_view a) {
    return Recipe{{cell(a)}, output, count, true};
}

/// Every recipe, in match order.
///
/// **The order matters only for ambiguity, and there is none today**: no two of these
/// match the same grid. The first match wins, so a recipe added later that is a
/// superset of an earlier one would never fire -- worth knowing before adding the
/// ones Phase 17 brings, where a furnace and a bench are both four of something.
inline constexpr std::array kRecipes{
    // A log makes four planks, wherever it is put. The first step of every game of
    // Minecraft, and the first recipe that had to work here.
    shapeless1(itemIdOf("oak_planks"), 4, "oak_log"),

    // Two planks, one above the other, make four sticks. Shaped rather than
    // shapeless because "two planks anywhere" would also be how a player accidentally
    // turns a stack of building material into sticks.
    shaped(itemIdOf("stick"), 4,
           "oak_planks", "", "",
           "oak_planks", "", "",
           "",           "", ""),

    // -- wooden tools ----------------------------------------------------------
    shaped(itemIdOf("wooden_pickaxe"), 1,
           "oak_planks", "oak_planks", "oak_planks",
           "",           "stick",      "",
           "",           "stick",      ""),
    shaped(itemIdOf("wooden_axe"), 1,
           "oak_planks", "oak_planks", "",
           "oak_planks", "stick",      "",
           "",           "stick",      ""),
    shaped(itemIdOf("wooden_shovel"), 1,
           "oak_planks", "", "",
           "stick",      "", "",
           "stick",      "", ""),
    shaped(itemIdOf("wooden_sword"), 1,
           "oak_planks", "", "",
           "oak_planks", "", "",
           "stick",      "", ""),

    // -- stone tools -----------------------------------------------------------
    // Cobblestone, not stone: stone drops cobblestone, so cobblestone is what a
    // player actually has. Vanilla's recipes say the same thing for the same reason.
    shaped(itemIdOf("stone_pickaxe"), 1,
           "cobblestone", "cobblestone", "cobblestone",
           "",            "stick",       "",
           "",            "stick",       ""),
    shaped(itemIdOf("stone_axe"), 1,
           "cobblestone", "cobblestone", "",
           "cobblestone", "stick",       "",
           "",            "stick",       ""),
    shaped(itemIdOf("stone_shovel"), 1,
           "cobblestone", "", "",
           "stick",       "", "",
           "stick",       "", ""),
    shaped(itemIdOf("stone_sword"), 1,
           "cobblestone", "", "",
           "cobblestone", "", "",
           "stick",       "", ""),
};

/// What a 3x3 grid of items produces.
struct CraftResult {
    ItemId item = kNoItem;
    u32 count = 0;

    bool empty() const noexcept { return item == kNoItem || count == 0; }
};

/// Matches `grid` against every recipe and returns the first that fits.
///
/// Only *which* item is in each cell matters, never how many: a recipe consumes one
/// from each occupied cell, so a grid of full stacks crafts once and can be clicked
/// again. That is vanilla's behaviour and it is why this takes ids rather than stacks.
///
/// **Shaped recipes match anywhere in the grid and match mirrored.** Both are
/// vanilla. Anywhere, because a player who builds a pickaxe in the bottom-left three
/// columns has built a pickaxe; mirrored, because an axe is the only tool whose
/// pattern is not symmetric and demanding one handedness of it is a rule nobody could
/// guess.
CraftResult matchRecipe(const std::array<ItemId, 9>& grid);

} // namespace mc
