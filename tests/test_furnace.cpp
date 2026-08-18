#include "world/BlockTable.hpp"
#include "world/Furnace.hpp"
#include "world/Inventory.hpp"
#include "world/Screen.hpp"
#include "world/Smelting.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

/// Loads a furnace with an ingredient and a fuel, the way a player would.
///
/// **Fills in place rather than returning one.** `Container` deletes its copy and
/// move operations on purpose -- a `Screen` holds a pointer to one, and a container
/// that could be moved out from under a screen is a dangling pointer waiting to
/// happen. The test has to be written the way the engine is.
void load(Furnace& furnace, ItemId input, u32 inputCount, ItemId fuel, u32 fuelCount) {
    furnace.mutableAt(Furnace::kInputSlot) = ItemStack{input, inputCount};
    furnace.mutableAt(Furnace::kFuelSlot) = ItemStack{fuel, fuelCount};
}

} // namespace

TEST_CASE("the smelting table answers what an ore becomes") {
    CHECK(smeltOutput(itemIdOf("iron_ore")) == itemIdOf("iron_ingot"));
    CHECK(smeltOutput(itemIdOf("deepslate_iron_ore")) == itemIdOf("iron_ingot"));
    CHECK(smeltOutput(itemIdOf("gold_ore")) == itemIdOf("gold_ingot"));
    CHECK(smeltOutput(itemIdOf("cobblestone")) == itemIdOf("stone"));

    // Not everything smelts, and the answer for those is nothing rather than itself.
    CHECK(smeltOutput(itemIdOf("dirt")) == kNoItem);
    CHECK(smeltOutput(itemIdOf("stick")) == kNoItem);
    CHECK(smeltOutput(kNoItem) == kNoItem);
}

TEST_CASE("coal is worth eight smelts and a plank is worth one and a half") {
    // The ratio is what makes coal worth going to look for. If a plank were as good
    // there would be no reason to mine at all.
    CHECK(burnTicks(itemIdOf("coal")) == 8 * kSmeltTicks);
    CHECK(burnTicks(itemIdOf("oak_planks")) == 300);
    CHECK(burnTicks(itemIdOf("stick")) == 100);

    CHECK(isFuel(itemIdOf("coal")));
    CHECK_FALSE(isFuel(itemIdOf("iron_ore")));
    CHECK_FALSE(isFuel(kNoItem));
}

TEST_CASE("a loaded furnace smelts one item in two hundred ticks") {
    Furnace furnace;
    load(furnace, itemIdOf("iron_ore"), 1, itemIdOf("coal"), 1);

    // Nothing yet: the first tick lights the fuel and starts the clock.
    furnace.tick(1);
    CHECK(furnace.burning());
    CHECK(furnace.at(Furnace::kOutputSlot).empty());

    furnace.tick(kSmeltTicks - 1);
    CHECK(furnace.at(Furnace::kOutputSlot).item == itemIdOf("iron_ingot"));
    CHECK(furnace.at(Furnace::kOutputSlot).count == 1);

    // The ore is gone and the coal was consumed once, at lighting.
    CHECK(furnace.at(Furnace::kInputSlot).empty());
    CHECK(furnace.at(Furnace::kFuelSlot).empty());
}

TEST_CASE("one coal smelts eight ores and no more") {
    Furnace furnace;
    load(furnace, itemIdOf("iron_ore"), 16, itemIdOf("coal"), 1);

    furnace.tick(8 * kSmeltTicks);

    CHECK(furnace.at(Furnace::kOutputSlot).count == 8);
    CHECK(furnace.at(Furnace::kInputSlot).count == 8);
    CHECK_FALSE(furnace.burning());

    // And it stays at eight, because there is nothing left to burn.
    furnace.tick(kSmeltTicks * 4);
    CHECK(furnace.at(Furnace::kOutputSlot).count == 8);
}

TEST_CASE("a furnace with no fuel does nothing at all") {
    Furnace furnace;
    load(furnace, itemIdOf("iron_ore"), 4, kNoItem, 0);
    furnace.tick(kSmeltTicks * 3);

    CHECK_FALSE(furnace.burning());
    CHECK(furnace.at(Furnace::kOutputSlot).empty());
    CHECK(furnace.at(Furnace::kInputSlot).count == 4);
}

TEST_CASE("a furnace with nothing to smelt does not burn its fuel") {
    // **The rule that makes filling the input first the right move.** Vanilla lights
    // the fire only when there is something to heat, so a furnace holding coal and
    // nothing else is not quietly eating it.
    Furnace furnace;
    load(furnace, kNoItem, 0, itemIdOf("coal"), 3);
    furnace.tick(kSmeltTicks * 2);

    CHECK_FALSE(furnace.burning());
    CHECK(furnace.at(Furnace::kFuelSlot).count == 3);
}

TEST_CASE("an unsmeltable ingredient is not smelted and burns nothing") {
    Furnace furnace;
    load(furnace, itemIdOf("dirt"), 8, itemIdOf("coal"), 1);
    furnace.tick(kSmeltTicks * 2);

    CHECK(furnace.at(Furnace::kOutputSlot).empty());
    CHECK(furnace.at(Furnace::kInputSlot).count == 8);
    CHECK(furnace.at(Furnace::kFuelSlot).count == 1);
}

TEST_CASE("a full output stops the furnace rather than destroying what it made") {
    Furnace furnace;
    load(furnace, itemIdOf("iron_ore"), 8, itemIdOf("coal"), 4);
    furnace.mutableAt(Furnace::kOutputSlot) =
        ItemStack{itemIdOf("iron_ingot"), maxStackOf(itemIdOf("iron_ingot"))};

    furnace.tick(kSmeltTicks * 3);

    // The ore is untouched and the output did not overflow. A furnace that smelted
    // into a full slot would delete the result, which is a loss with no explanation.
    CHECK(furnace.at(Furnace::kInputSlot).count == 8);
    CHECK(furnace.at(Furnace::kOutputSlot).count == maxStackOf(itemIdOf("iron_ingot")));
}

TEST_CASE("the output slot cannot be put into") {
    // The whole reason `SlotKind::Output` exists. A player who could drop cobblestone
    // into the output has smelted nothing and gained an ingot's worth of nothing.
    Furnace furnace;
    CHECK(furnace.kindOf(Furnace::kOutputSlot) == SlotKind::Output);
    CHECK(furnace.kindOf(Furnace::kInputSlot) == SlotKind::Normal);
    CHECK(furnace.kindOf(Furnace::kFuelSlot) == SlotKind::Normal);

    Inventory inventory;
    Screen screen{inventory, furnace};
    inventory.add(itemIdOf("cobblestone"), 4);

    screen.click(screen.containerSlots()); // pick the cobblestone up
    REQUIRE(inventory.cursor().item == itemIdOf("cobblestone"));

    screen.click(Furnace::kOutputSlot);
    // Still in hand, and the output is still empty.
    CHECK(inventory.cursor().item == itemIdOf("cobblestone"));
    CHECK(furnace.at(Furnace::kOutputSlot).empty());
}

TEST_CASE("taking the output takes the whole stack") {
    // Unlike a crafting result, which is produced by the click and comes one at a
    // time. This one was produced by the fire and is already sitting there.
    Furnace furnace;
    load(furnace, itemIdOf("iron_ore"), 4, itemIdOf("coal"), 1);
    furnace.tick(kSmeltTicks * 4);
    REQUIRE(furnace.at(Furnace::kOutputSlot).count == 4);

    Inventory inventory;
    Screen screen{inventory, furnace};
    screen.click(Furnace::kOutputSlot);

    CHECK(inventory.cursor().item == itemIdOf("iron_ingot"));
    CHECK(inventory.cursor().count == 4);
    CHECK(furnace.at(Furnace::kOutputSlot).empty());
}

TEST_CASE("closing a furnace's window leaves its contents alone") {
    // **The difference between a machine and a grid**, and why `releaseOne` is
    // virtual. A crafting table hands everything back; a furnace keeps burning.
    Furnace furnace;
    load(furnace, itemIdOf("iron_ore"), 2, itemIdOf("coal"), 1);
    Inventory inventory;
    Screen screen{inventory, furnace};

    furnace.tick(10);
    while (screen.releaseOne().moved) {
    }

    CHECK(furnace.at(Furnace::kInputSlot).count == 2);
    CHECK(furnace.burning());
    CHECK(inventory.usedSlots() == 0);
}

TEST_CASE("an untouched furnace is idle and a working one is not") {
    // What lets `Engine` forget a furnace the player only glanced inside.
    Furnace empty;
    CHECK(empty.idle());

    Furnace working;
    load(working, itemIdOf("iron_ore"), 1, itemIdOf("coal"), 1);
    CHECK_FALSE(working.idle());

    working.tick(kSmeltTicks * 8);
    CHECK_FALSE(working.idle()); // It holds an ingot now.
}

TEST_CASE("the iron chain is reachable and the diamond one follows it") {
    // **The wall Phase 16 stopped at, checked from both sides.** Iron ore needs a
    // stone pickaxe to drop; the ore smelts to an ingot; the ingot makes a pickaxe
    // that is the first thing able to harvest diamond.
    CHECK(dropOf(blockIdOf("iron_ore"), itemIdOf("wooden_pickaxe")) == kNoItem);
    CHECK(dropOf(blockIdOf("iron_ore"), itemIdOf("stone_pickaxe")) == itemIdOf("iron_ore"));

    CHECK(smeltOutput(itemIdOf("iron_ore")) == itemIdOf("iron_ingot"));

    CHECK(dropOf(blockIdOf("diamond_ore"), itemIdOf("stone_pickaxe")) == kNoItem);
    CHECK(dropOf(blockIdOf("diamond_ore"), itemIdOf("iron_pickaxe")) == itemIdOf("diamond"));

    // And the far end of it: a diamond pickaxe exists to be made.
    CHECK(toolTierOf(itemIdOf("diamond_pickaxe")) == ToolTier::Diamond);
}
