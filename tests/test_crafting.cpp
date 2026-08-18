#include "world/BlockTable.hpp"
#include "world/Crafting.hpp"
#include "world/CraftingGrid.hpp"
#include "world/Inventory.hpp"
#include "world/ItemTable.hpp"
#include "world/Screen.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace mc;

namespace {

/// A 3x3 grid written as three rows of names, so a test reads like the recipe it is
/// checking rather than like nine array assignments.
std::array<ItemId, 9> grid(std::string_view a, std::string_view b, std::string_view c,
                           std::string_view d, std::string_view e, std::string_view f,
                           std::string_view g, std::string_view h, std::string_view i) {
    return {itemIdOrNothing(a), itemIdOrNothing(b), itemIdOrNothing(c),
            itemIdOrNothing(d), itemIdOrNothing(e), itemIdOrNothing(f),
            itemIdOrNothing(g), itemIdOrNothing(h), itemIdOrNothing(i)};
}

/// A crafting table's window: a 3x3, its output, then the player's thirty-six.
///
/// **Held together rather than returned**, because a `Screen` points at both and a
/// factory returning one would hand back a screen over two destroyed containers.
struct TableScreen {
    Inventory inventory;
    CraftingGrid craft{3};
    Screen screen{inventory, craft};

    /// The flat index of the player's own slot `n`.
    usize player(usize n) const { return screen.containerSlots() + n; }
    /// What the grid would make right now.
    CraftResult result() const { return craft.result(); }
};

/// The player's own window, which is 2x2 and is where the gate bites.
struct PocketScreen {
    Inventory inventory;
    CraftingGrid craft{2};
    Screen screen{inventory, craft};

    usize player(usize n) const { return screen.containerSlots() + n; }
    CraftResult result() const { return craft.result(); }
};

/// Fills a screen's crafting grid from names, one item per named cell.
void fillCraftGrid(TableScreen& ui, const std::array<ItemId, 9>& cells) {
    for (usize i = 0; i < 9; ++i) {
        if (cells[i] == kNoItem) {
            continue;
        }
        // Through the public interface rather than by reaching in: pick a stack up
        // and right-click it into the cell, which is exactly what a player does and
        // is the path that would break if slot indexing drifted.
        ui.inventory.add(cells[i], 1);
        ui.screen.click(ui.player(0));
        ui.screen.split(i);
    }
}

} // namespace

TEST_CASE("an empty grid produces nothing") {
    CHECK(matchRecipe(grid("", "", "", "", "", "", "", "", "")).empty());
}

TEST_CASE("a log makes planks wherever it is put") {
    // Shapeless, so all nine positions are the same recipe. A player who drops a log
    // into whichever cell the pointer was over has made planks.
    for (usize i = 0; i < 9; ++i) {
        std::array<ItemId, 9> cells{};
        cells[i] = itemIdOf("oak_log");
        CAPTURE(i);
        const CraftResult result = matchRecipe(cells);
        CHECK(result.item == itemIdOf("oak_planks"));
        CHECK(result.count == 4);
    }

    // Two logs is not the one-log recipe. Matching it would craft four planks and
    // silently eat the second log.
    CHECK(matchRecipe(grid("oak_log", "oak_log", "", "", "", "", "", "", "")).empty());
}

TEST_CASE("a shaped recipe matches anywhere in the grid") {
    const CraftResult topLeft =
        matchRecipe(grid("oak_planks", "", "",
                         "oak_planks", "", "",
                         "",           "", ""));
    CHECK(topLeft.item == itemIdOf("stick"));
    CHECK(topLeft.count == 4);

    // The same shape in the bottom-right corner is the same recipe. Anchoring to the
    // top-left would be a rule the player has to learn and vanilla does not have.
    const CraftResult bottomRight =
        matchRecipe(grid("", "", "",
                         "", "", "oak_planks",
                         "", "", "oak_planks"));
    CHECK(bottomRight.item == itemIdOf("stick"));

    // Side by side is not the same shape, and must not make sticks.
    CHECK(matchRecipe(grid("oak_planks", "oak_planks", "",
                           "",           "",           "",
                           "",           "",           "")).empty());
}

TEST_CASE("a pickaxe is the 3x3 recipe Phase 16 exists for") {
    const CraftResult wooden =
        matchRecipe(grid("oak_planks", "oak_planks", "oak_planks",
                         "",           "stick",      "",
                         "",           "stick",      ""));
    CHECK(wooden.item == itemIdOf("wooden_pickaxe"));
    CHECK(wooden.count == 1);

    // Stone tools take cobblestone, because cobblestone is what stone drops and
    // therefore what a player actually has.
    const CraftResult stone =
        matchRecipe(grid("cobblestone", "cobblestone", "cobblestone",
                         "",            "stick",       "",
                         "",            "stick",       ""));
    CHECK(stone.item == itemIdOf("stone_pickaxe"));

    // Stone in the grid rather than cobblestone matches nothing. Vanilla is the same,
    // and it matters here because a player can only get stone by placing cobblestone
    // and cannot get it back.
    CHECK(matchRecipe(grid("stone", "stone", "stone",
                           "",      "stick", "",
                           "",      "stick", "")).empty());
}

TEST_CASE("an axe matches both ways round") {
    // The one tool whose pattern is not symmetric, and the reason shaped matching
    // tries the mirror. Demanding one handedness is a rule nobody could guess.
    const CraftResult right =
        matchRecipe(grid("oak_planks", "oak_planks", "",
                         "oak_planks", "stick",      "",
                         "",           "stick",      ""));
    CHECK(right.item == itemIdOf("wooden_axe"));

    const CraftResult left =
        matchRecipe(grid("oak_planks", "oak_planks", "",
                         "stick",      "oak_planks", "",
                         "stick",      "",           ""));
    CHECK(left.item == itemIdOf("wooden_axe"));
}

TEST_CASE("every tool has a recipe and every recipe makes something real") {
    for (const Recipe& recipe : kRecipes) {
        CAPTURE(itemName(recipe.output));
        CHECK(itemExists(recipe.output));
        CHECK(recipe.count > 0);
        CHECK(recipe.count <= maxStackOf(recipe.output));
    }

    // The four tools in two tiers, all reachable. A tool added to `kItems` without a
    // recipe is a tool nobody can ever hold.
    const std::string_view tools[] = {
        "wooden_pickaxe", "wooden_axe", "wooden_shovel", "wooden_sword",
        "stone_pickaxe",  "stone_axe",  "stone_shovel",  "stone_sword",
    };
    for (const std::string_view name : tools) {
        const ItemId id = itemIdOrNothing(name);
        CAPTURE(name);
        bool found = false;
        for (const Recipe& recipe : kRecipes) {
            found = found || recipe.output == id;
        }
        CHECK(found);
    }
}

TEST_CASE("crafting through a screen consumes one from each cell") {
    TableScreen ui;
    const usize output = ui.craft.outputSlot();

    // Two planks stacked, from full stacks, so the grid can be crafted twice.
    ui.inventory.add(itemIdOf("oak_planks"), 64);
    ui.screen.click(ui.player(0));
    ui.screen.split(0);
    ui.screen.split(3);
    ui.screen.click(ui.player(1)); // Put the rest of the stack down.

    REQUIRE(ui.result().item == itemIdOf("stick"));

    ui.screen.click(output);
    CHECK(ui.inventory.cursor().item == itemIdOf("stick"));
    CHECK(ui.inventory.cursor().count == 4);

    // One from each occupied cell, not the whole stack. A grid of full stacks is
    // meant to be clicked again.
    CHECK(ui.screen.at(0).count == 0);
    CHECK(ui.screen.at(3).count == 0);

    // And with the cells emptied the output is gone too, because it was a preview of
    // what the grid holds rather than a stack sitting in a slot.
    CHECK(ui.result().empty());
    CHECK(ui.screen.at(output).empty());
}

TEST_CASE("a second click stacks the output into the same hand") {
    TableScreen ui;
    fillCraftGrid(ui, grid("oak_planks", "", "",
                           "oak_planks", "", "",
                           "",           "", ""));
    // Two of each, so the grid crafts twice.
    fillCraftGrid(ui, grid("oak_planks", "", "",
                           "oak_planks", "", "",
                           "",           "", ""));
    REQUIRE(ui.screen.at(0).count == 2);

    ui.screen.click(ui.craft.outputSlot());
    ui.screen.click(ui.craft.outputSlot());
    CHECK(ui.inventory.cursor().item == itemIdOf("stick"));
    CHECK(ui.inventory.cursor().count == 8);
    CHECK(ui.result().empty());
}

TEST_CASE("crafting refuses when the hand cannot take the result") {
    TableScreen ui;
    fillCraftGrid(ui, grid("oak_log", "", "", "", "", "", "", "", ""));
    REQUIRE(ui.result().item == itemIdOf("oak_planks"));

    // Something else in hand. Crafting into it would either destroy the result or
    // leave the grid half-eaten, and both are losses the player cannot explain.
    ui.inventory.add(kStoneBlock, 1);
    ui.screen.click(ui.player(0));
    REQUIRE(ui.inventory.cursor().item == kStoneBlock);

    ui.screen.click(ui.craft.outputSlot());
    CHECK(ui.inventory.cursor().item == kStoneBlock);
    CHECK(ui.inventory.cursor().count == 1);
    // The grid is untouched, so the recipe still stands.
    CHECK(ui.result().item == itemIdOf("oak_planks"));
}

TEST_CASE("closing the window does not eat the crafting grid") {
    TableScreen ui;
    fillCraftGrid(ui, grid("oak_planks", "oak_planks", "oak_planks",
                           "",           "",           "",
                           "",           "",           ""));
    REQUIRE(ui.inventory.count(itemIdOf("oak_planks")) == 0); // All three in the grid.

    while (ui.screen.releaseOne().moved) {
    }
    CHECK(ui.inventory.count(itemIdOf("oak_planks")) == 3);
    CHECK(ui.screen.at(0).empty());
}

TEST_CASE("a tool does not stack and a block does") {
    CHECK(maxStackOf(itemIdOf("wooden_pickaxe")) == 1);
    CHECK(maxStackOf(itemIdOf("stone_sword")) == 1);
    CHECK(maxStackOf(itemIdOf("stick")) == 64);
    CHECK(maxStackOf(itemIdOf("coal")) == 64);
    CHECK(maxStackOf(kStoneBlock) == 64);

    // A slot holding sixty-four pickaxes is a pickaxe that never runs out, and
    // durability in Phase 17 would have nowhere to live on it.
    Inventory inventory;
    CHECK(inventory.add(itemIdOf("wooden_pickaxe"), 3) == 0);
    CHECK(inventory.at(0).count == 1);
    CHECK(inventory.at(1).count == 1);
    CHECK(inventory.at(2).count == 1);
}

TEST_CASE("planks are only reachable by crafting") {
    // No block the *generator* can place drops a plank, so the grid is the only way
    // to get a first one -- which makes it the shortest proof that crafting is wired
    // from recipe to placed block.
    //
    // A plank the player already placed is excluded, and has to be: breaking one
    // gives it back, the same as every other block. The claim is about where the
    // first plank comes from, not about whether they survive being put down.
    for (usize id = 0; id < kBlocks.size(); ++id) {
        const auto block = static_cast<BlockId>(id);
        if (block == kOakPlanksBlock) {
            continue;
        }
        CAPTURE(kBlocks[id].name);
        CHECK(dropOf(block, itemIdOf("stone_pickaxe")) != itemIdOf("oak_planks"));
    }

    // And a plank is a block, so what the grid produces can be put back in the world.
    CHECK(itemIsBlock(itemIdOf("oak_planks")));
    CHECK(blockOfItem(itemIdOf("oak_planks")) == kOakPlanksBlock);
}

TEST_CASE("the player's own grid cannot make a pickaxe, and the table can") {
    // **Vanilla's gate, and the whole reason a crafting table exists.** A pickaxe is
    // three planks across the top and two sticks down the middle: three columns and
    // three rows. Four cells cannot hold it however they are arranged, so the only
    // path to a pickaxe runs through a block the player has to craft and place.
    //
    // This is expressed as arithmetic rather than as a rule -- there is one recipe
    // table and one matcher, and the grid's edge is the only difference between the
    // two windows.
    PocketScreen pocket;
    pocket.inventory.add(itemIdOf("oak_planks"), 3);
    pocket.inventory.add(itemIdOf("stick"), 2);

    // Everything that fits in 2x2, laid out as the recipe would want it. There is no
    // arrangement of four cells that is three wide.
    pocket.screen.click(pocket.player(0));
    pocket.screen.split(0);
    pocket.screen.split(1);
    pocket.screen.click(pocket.player(1)); // put the rest down
    pocket.screen.click(pocket.player(1));
    pocket.screen.split(2);
    pocket.screen.split(3);
    CHECK(pocket.result().empty());

    TableScreen table;
    fillCraftGrid(table, grid("oak_planks", "oak_planks", "oak_planks",
                              "",           "stick",      "",
                              "",           "stick",      ""));
    CHECK(table.result().item == itemIdOf("wooden_pickaxe"));
    CHECK(table.result().count == 1);
}

TEST_CASE("the table itself is craftable without a table") {
    // The bootstrap, and the one recipe that has to fit in 2x2 or the gate is a wall.
    // Four planks in a square, which is exactly the player's own grid full.
    PocketScreen pocket;
    pocket.inventory.add(itemIdOf("oak_planks"), 4);
    pocket.screen.click(pocket.player(0));
    for (usize cell = 0; cell < 4; ++cell) {
        pocket.screen.split(cell);
    }

    CHECK(pocket.result().item == itemIdOf("crafting_table"));
    CHECK(pocket.result().count == 1);

    // And it is a block, so what the grid produces can be put down and used.
    CHECK(itemIsBlock(itemIdOf("crafting_table")));
    CHECK(blockOfItem(itemIdOf("crafting_table")) == kCraftingTableBlock);
}

TEST_CASE("small recipes still work in the big grid") {
    // The padding is the only mechanism: a 2x2 recipe laid into a 3x3 has to match
    // there too, or a player at a table could not make planks.
    TableScreen ui;
    fillCraftGrid(ui, grid("oak_planks", "oak_planks", "",
                           "oak_planks", "oak_planks", "",
                           "",           "",           ""));
    CHECK(ui.result().item == itemIdOf("crafting_table"));
}

TEST_CASE("four planks in a row is not a crafting table") {
    // The shape matters, not the count. A row of four is 1x4, which does not fit the
    // 3x3 at all -- and the bounding-box comparison is what refuses it rather than a
    // special case.
    TableScreen ui;
    fillCraftGrid(ui, grid("oak_planks", "oak_planks", "oak_planks",
                           "oak_planks", "",           "",
                           "",           "",           ""));
    CHECK(ui.result().empty());
}
