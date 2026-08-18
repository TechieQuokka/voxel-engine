#include "world/BlockTable.hpp"
#include "world/Crafting.hpp"
#include "world/CraftingGrid.hpp"
#include "world/Furnace.hpp"
#include "world/Inventory.hpp"
#include "world/ItemTable.hpp"
#include "world/Screen.hpp"
#include "world/Smelting.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string_view>

using namespace mc;

// **The whole chain, in one case, in the order a player walks it.**
//
// Every step here has its own tests already -- recipes in test_crafting, slots in
// test_inventory, smelting in test_furnace, harvest tiers in test_items. What none
// of them check is that each step's *output* is the next step's *input*, and that is
// exactly the shape of bug this project has shipped before: item pickup passed six
// unit tests for four play sessions while being broken, because the defect was in the
// relationship between two constants that no single unit owned.
//
// This is the logic half of walking to a diamond. It cannot press a mouse button or
// aim at a block, so mining is `dropOf` and the tier gates are `canHarvest` -- the
// same functions the engine calls. What it does prove is that nothing in the chain
// hands the next step something it cannot use.

namespace {

/// A window over an inventory: 2 for the player's own grid, 3 for a crafting table.
///
/// Held together rather than returned, because a `Screen` points at both and a
/// factory returning one would hand back a screen over two destroyed containers.
struct Bench {
    Inventory inventory;
    CraftingGrid craft;
    Screen screen;

    explicit Bench(usize edge) : craft(edge), screen(inventory, craft) {}

    usize player(usize n) const { return screen.containerSlots() + n; }
};

/// Moves one of `item` from the player's pack into grid cell `cell`.
///
/// Through `click` and `split`, which is what the pointer does, rather than by
/// reaching into the cells: the path that would break if slot indexing drifted is
/// the one worth exercising.
void placeInGrid(Bench& bench, ItemId item, usize cell) {
    REQUIRE(bench.inventory.count(item) > 0);

    // Find the pack slot holding it, pick the stack up, and drop one into the cell.
    for (usize slot = 0; slot < Inventory::kStorageSlots; ++slot) {
        if (bench.inventory.at(slot).item == item && !bench.inventory.at(slot).empty()) {
            bench.screen.click(bench.player(slot));
            bench.screen.split(cell);
            break;
        }
    }

    // Whatever is left in hand goes back where it came from, so the next step starts
    // with an empty cursor exactly as a player's does.
    if (!bench.inventory.cursorEmpty()) {
        for (usize slot = 0; slot < Inventory::kStorageSlots; ++slot) {
            if (bench.inventory.at(slot).empty()) {
                bench.screen.click(bench.player(slot));
                break;
            }
        }
    }
    REQUIRE(bench.inventory.cursorEmpty());
}

/// Lays out a recipe by name and takes what it makes into the pack.
///
/// Returns the crafted stack's item, so a caller can assert on what came out rather
/// than on what it asked for -- which is the difference between testing the chain and
/// testing the test.
ItemId craftFromPack(Bench& bench, const std::array<std::string_view, 9>& cells) {
    const usize edge = bench.craft.edge();

    for (usize row = 0; row < edge; ++row) {
        for (usize column = 0; column < edge; ++column) {
            const std::string_view name = cells[row * 3 + column];
            if (name.empty()) {
                continue;
            }
            placeInGrid(bench, itemIdOrNothing(name), row * edge + column);
        }
    }

    const CraftResult result = bench.craft.result();
    REQUIRE_FALSE(result.empty());

    // Take it, then put the hand down into the pack. Two clicks, as with a pointer.
    bench.screen.click(bench.craft.outputSlot());
    REQUIRE(bench.inventory.cursor().item == result.item);

    for (usize slot = 0; slot < Inventory::kStorageSlots; ++slot) {
        if (bench.inventory.at(slot).empty()) {
            bench.screen.click(bench.player(slot));
            break;
        }
    }
    REQUIRE(bench.inventory.cursorEmpty());

    return result.item;
}

/// What a block gives up when it is broken with `held`, which is what the engine
/// spawns as a dropped item.
///
/// `blockIdOf` is consteval and these names arrive as arguments, so the lookup goes
/// through the runtime form. **That it works at all is the property Phase 16 bought**:
/// item ids extend the block id space rather than replacing it, so a block's name
/// resolves to the same number in both.
ItemId mine(std::string_view block, ItemId held) {
    const ItemId id = itemIdOrNothing(block);
    REQUIRE(id != kNoItem);
    return dropOf(static_cast<BlockId>(id), held);
}

constexpr std::string_view kNothing{};

} // namespace

TEST_CASE("the whole chain from a tree to a diamond") {
    // ---------------------------------------------------------------------------
    // A tree, by hand. Every game of this starts here, and wood is the one thing in
    // the world that needs no tool at all.
    // ---------------------------------------------------------------------------
    Bench pocket{2};
    const ItemId log = mine("oak_log", kNoItem);
    REQUIRE(log == itemIdOf("oak_log"));
    pocket.inventory.add(log, 8);

    // ---------------------------------------------------------------------------
    // The 2x2 the player carries. Planks, sticks, and the table -- and *not* a
    // pickaxe, which is the gate the crafting table exists to be.
    // ---------------------------------------------------------------------------
    CHECK(craftFromPack(pocket, {"oak_log", kNothing, kNothing,
                                 kNothing,  kNothing, kNothing,
                                 kNothing,  kNothing, kNothing})
          == itemIdOf("oak_planks"));
    CHECK(pocket.inventory.count(itemIdOf("oak_planks")) == 4);

    // A pickaxe is three across the top and two down the middle, which does not fit
    // in four cells however it is arranged. This is vanilla's rule and the reason a
    // table is the second thing anyone makes.
    CHECK(matchRecipe({itemIdOf("oak_planks"), itemIdOf("oak_planks"), kNoItem,
                       itemIdOf("stick"), kNoItem, kNoItem,
                       kNoItem, kNoItem, kNoItem})
              .empty());

    craftFromPack(pocket, {"oak_log", kNothing, kNothing,
                           kNothing,  kNothing, kNothing,
                           kNothing,  kNothing, kNothing});
    CHECK(craftFromPack(pocket, {"oak_planks", kNothing, kNothing,
                                 "oak_planks", kNothing, kNothing,
                                 kNothing,     kNothing, kNothing})
          == itemIdOf("stick"));

    CHECK(craftFromPack(pocket, {"oak_planks", "oak_planks", kNothing,
                                 "oak_planks", "oak_planks", kNothing,
                                 kNothing,     kNothing,     kNothing})
          == itemIdOf("crafting_table"));

    // ---------------------------------------------------------------------------
    // The table. Everything past here is 3x3.
    // ---------------------------------------------------------------------------
    Bench table{3};
    table.inventory.add(itemIdOf("oak_planks"), 3);
    table.inventory.add(itemIdOf("stick"), 8);

    const ItemId woodenPickaxe =
        craftFromPack(table, {"oak_planks", "oak_planks", "oak_planks",
                              kNothing,     "stick",      kNothing,
                              kNothing,     "stick",      kNothing});
    CHECK(woodenPickaxe == itemIdOf("wooden_pickaxe"));

    // ---------------------------------------------------------------------------
    // Stone. **Bare hands give nothing**, which is the rule that makes the pickaxe
    // worth the trouble rather than merely faster.
    // ---------------------------------------------------------------------------
    CHECK(mine("stone", kNoItem) == kNoItem);
    CHECK(mine("stone", woodenPickaxe) == itemIdOf("cobblestone"));

    // And what it gives is cobblestone, not stone -- so the stone tools below have to
    // be written against cobblestone, and they are.
    table.inventory.add(mine("stone", woodenPickaxe), 20);

    const ItemId stonePickaxe =
        craftFromPack(table, {"cobblestone", "cobblestone", "cobblestone",
                              kNothing,      "stick",       kNothing,
                              kNothing,      "stick",       kNothing});
    CHECK(stonePickaxe == itemIdOf("stone_pickaxe"));

    // ---------------------------------------------------------------------------
    // Iron ore, which wood cannot take and stone can. The tier gate is vanilla's and
    // it is the reason the furnace comes before the ingot.
    // ---------------------------------------------------------------------------
    CHECK(mine("iron_ore", woodenPickaxe) == kNoItem);
    const ItemId ironOre = mine("iron_ore", stonePickaxe);
    CHECK(ironOre == itemIdOf("iron_ore"));

    // ---------------------------------------------------------------------------
    // A furnace: eight cobblestone in a ring, with the middle deliberately empty.
    // ---------------------------------------------------------------------------
    const ItemId furnaceItem =
        craftFromPack(table, {"cobblestone", "cobblestone", "cobblestone",
                              "cobblestone", kNothing,      "cobblestone",
                              "cobblestone", "cobblestone", "cobblestone"});
    CHECK(furnaceItem == itemIdOf("furnace"));

    // ---------------------------------------------------------------------------
    // Smelting. Ore on top, coal underneath, and ten seconds of the 20 Hz tick.
    // ---------------------------------------------------------------------------
    Furnace furnace;
    furnace.mutableAt(Furnace::kInputSlot) = ItemStack{ironOre, 1};
    furnace.mutableAt(Furnace::kFuelSlot) = ItemStack{itemIdOf("coal"), 1};

    // One tick short of the smelt, so the boundary is where the assertion lands
    // rather than somewhere comfortably past it.
    furnace.tick(kSmeltTicks - 1);
    CHECK(furnace.at(Furnace::kOutputSlot).empty());

    furnace.tick(1);
    const ItemStack smelted = furnace.at(Furnace::kOutputSlot);
    REQUIRE_FALSE(smelted.empty());
    CHECK(smelted.item == itemIdOf("iron_ingot"));

    // ---------------------------------------------------------------------------
    // The iron pickaxe, and the only thing in the world that gets a diamond out of
    // the ground.
    // ---------------------------------------------------------------------------
    table.inventory.add(smelted.item, 3);
    const ItemId ironPickaxe =
        craftFromPack(table, {"iron_ingot", "iron_ingot", "iron_ingot",
                              kNothing,     "stick",      kNothing,
                              kNothing,     "stick",      kNothing});
    CHECK(ironPickaxe == itemIdOf("iron_pickaxe"));

    CHECK(mine("diamond_ore", stonePickaxe) == kNoItem);
    const ItemId diamond = mine("diamond_ore", ironPickaxe);
    CHECK(diamond == itemIdOf("diamond"));

    // And the chain closes: what came out of the ground is what the last recipe wants.
    table.inventory.add(diamond, 3);
    CHECK(craftFromPack(table, {"diamond", "diamond", "diamond",
                                kNothing,  "stick",   kNothing,
                                kNothing,  "stick",   kNothing})
          == itemIdOf("diamond_pickaxe"));
}

TEST_CASE("one coal is eight smelts, and the ninth ore waits for more fuel") {
    // The fuel arithmetic in the same place as the chain that depends on it. A coal
    // burns 1,600 ticks and a smelt takes 200, so eight ores go through and the ninth
    // does not -- which was off by one when the furnace landed, and was found by a
    // person watching the flame rather than by any of this.
    Furnace furnace;
    furnace.mutableAt(Furnace::kFuelSlot) = ItemStack{itemIdOf("coal"), 1};

    u32 smelted = 0;
    for (u32 attempt = 0; attempt < 9; ++attempt) {
        furnace.mutableAt(Furnace::kInputSlot) = ItemStack{itemIdOf("iron_ore"), 1};
        furnace.tick(kSmeltTicks);

        if (furnace.at(Furnace::kInputSlot).empty()) {
            ++smelted;
        }
    }

    CHECK(smelted == burnTicks(itemIdOf("coal")) / kSmeltTicks);
    CHECK(smelted == 8);
}
