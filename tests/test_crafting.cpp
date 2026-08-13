#include "world/Crafting.hpp"
#include "world/Inventory.hpp"
#include "world/ItemTable.hpp"

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

/// Fills `inventory`'s crafting grid from names and returns what it produces.
void fillCraftGrid(Inventory& inventory, const std::array<ItemId, 9>& cells) {
    for (usize i = 0; i < Inventory::kCraftSlots; ++i) {
        if (cells[i] == kNoItem) {
            continue;
        }
        // Through the public interface rather than by reaching in: pick a stack up
        // and right-click it into the cell, which is exactly what a player does and
        // is the path that would break if slot indexing drifted.
        inventory.add(cells[i], 1);
        inventory.clickSlot(0);
        inventory.splitSlot(Inventory::kFirstCraftSlot + i);
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

TEST_CASE("crafting through the inventory consumes one from each cell") {
    Inventory inventory;

    // Two planks stacked, from full stacks, so the grid can be crafted twice.
    inventory.add(itemIdOf("oak_planks"), 64);
    inventory.clickSlot(0);
    inventory.splitSlot(Inventory::kFirstCraftSlot + 0);
    inventory.splitSlot(Inventory::kFirstCraftSlot + 3);
    inventory.clickSlot(1); // Put the rest of the stack down.

    REQUIRE(inventory.craftResult().item == itemIdOf("stick"));

    inventory.clickSlot(Inventory::kOutputSlot);
    CHECK(inventory.cursor().item == itemIdOf("stick"));
    CHECK(inventory.cursor().count == 4);

    // One from each occupied cell, not the whole stack. A grid of full stacks is
    // meant to be clicked again.
    CHECK(inventory.at(Inventory::kFirstCraftSlot + 0).count == 0);
    CHECK(inventory.at(Inventory::kFirstCraftSlot + 3).count == 0);

    // And with the cells emptied the output is gone too, because it was a preview of
    // what the grid holds rather than a stack sitting in a slot.
    CHECK(inventory.craftResult().empty());
    CHECK(inventory.at(Inventory::kOutputSlot).empty());
}

TEST_CASE("a second click stacks the output into the same hand") {
    Inventory inventory;
    fillCraftGrid(inventory, grid("oak_planks", "", "",
                                  "oak_planks", "", "",
                                  "",           "", ""));
    // Two of each, so the grid crafts twice.
    fillCraftGrid(inventory, grid("oak_planks", "", "",
                                  "oak_planks", "", "",
                                  "",           "", ""));
    REQUIRE(inventory.at(Inventory::kFirstCraftSlot).count == 2);

    inventory.clickSlot(Inventory::kOutputSlot);
    inventory.clickSlot(Inventory::kOutputSlot);
    CHECK(inventory.cursor().item == itemIdOf("stick"));
    CHECK(inventory.cursor().count == 8);
    CHECK(inventory.craftResult().empty());
}

TEST_CASE("crafting refuses when the hand cannot take the result") {
    Inventory inventory;
    fillCraftGrid(inventory, grid("oak_log", "", "", "", "", "", "", "", ""));
    REQUIRE(inventory.craftResult().item == itemIdOf("oak_planks"));

    // Something else in hand. Crafting into it would either destroy the result or
    // leave the grid half-eaten, and both are losses the player cannot explain.
    inventory.add(kStoneBlock, 1);
    inventory.clickSlot(0);
    REQUIRE(inventory.cursor().item == kStoneBlock);

    inventory.clickSlot(Inventory::kOutputSlot);
    CHECK(inventory.cursor().item == kStoneBlock);
    CHECK(inventory.cursor().count == 1);
    // The grid is untouched, so the recipe still stands.
    CHECK(inventory.craftResult().item == itemIdOf("oak_planks"));
}

TEST_CASE("closing the window does not eat the crafting grid") {
    Inventory inventory;
    fillCraftGrid(inventory, grid("oak_planks", "oak_planks", "oak_planks",
                                  "",           "",           "",
                                  "",           "",           ""));
    REQUIRE(inventory.count(itemIdOf("oak_planks")) == 0); // All three are in the grid.

    const ItemStack spilled = inventory.releaseCraftGrid();
    CHECK(spilled.empty()); // Storage had room, so nothing had to be dropped.
    CHECK(inventory.count(itemIdOf("oak_planks")) == 3);
    CHECK(inventory.at(Inventory::kFirstCraftSlot).empty());
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
