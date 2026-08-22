// The world a capture flag asks for, and nothing else.
//
// **This is fixture data, and it used to live in Engine's constructor** -- eighty-odd
// lines of it, a third of that function, between opening the save and creating the
// renderers. It is not initialization: none of it runs in an ordinary session, and
// every line exists so that one still frame shows something worth looking at. Keeping
// it next to the window and thread setup meant changing what a capture shows also
// meant editing the constructor, and reading the constructor meant reading a furnace
// recipe.
//
// They are still `Engine` members rather than free functions taking an `Engine&`,
// because they write private state -- the furnace map, the open screen, the player --
// and a friend declaration would buy nothing over this. What moves is the text.
//
// Ordering that the constructor still owns, because it is not local to this file:
//   - after `m_input`, since opening a screen releases the cursor and `m_input` owns it
//   - after the saved player is read, since these deliberately override it
//   - before the renderers, so a bad `--hold` name fails before a window exists

#include "app/Engine.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "world/BlockRegistry.hpp"

#include <initializer_list>

namespace mc {

void Engine::seedCaptureScenario() {
    if (m_options.openFurnace) {
        seedFurnaceScenario();
    } else if (m_options.openInventory) {
        seedInventoryScenario();
    }

    // Last, so it wins over whatever the two above put in slot 0.
    seedHeldItem();
}

void Engine::seedFurnaceScenario() {
    // A furnace part-way through a smelt, which is the only state worth a still
    // frame: an empty one shows three slots and proves nothing about the gauges.
    m_openFurnace = BlockPos{0, 0, 0};
    Furnace& furnace = m_furnaces[m_openFurnace];
    furnace.mutableAt(Furnace::kInputSlot) = ItemStack{itemIdOf("iron_ore"), 5};
    furnace.mutableAt(Furnace::kFuelSlot) = ItemStack{itemIdOf("coal"), 3};
    furnace.tick(kSmeltTicks + kSmeltTicks / 2); // One done, one half done.

    openScreen(ScreenKind::Furnace);

    m_player.inventory.add(itemIdOf("iron_ore"), 12);
    m_player.inventory.add(itemIdOf("coal"), 9);
    m_player.inventory.add(blockIdOf("furnace"), 2);
    m_player.inventory.add(itemIdOf("iron_ingot"), 4);
    m_player.inventory.add(itemIdOf("iron_pickaxe"), 1);
    m_player.inventory.add(itemIdOf("diamond_pickaxe"), 1);
    m_player.health = 13.0f;
}

void Engine::seedInventoryScenario() {
    // **The crafting table's window, not the player's**, because the 3x3 is where
    // a pickaxe is made and a 2x2 capture would show the smaller half of the
    // feature.
    openScreen(ScreenKind::CraftingTable);

    // **The grid is seeded first, and the order is load-bearing.** Items reach it
    // the way a player puts them there -- pick a stack up, right click into a
    // cell -- which needs the storage slot they landed in, and the only
    // predictable index is slot 0 of a pack that is still empty. Seeding this
    // after the display items below once asked `usedSlots()` for an index, which
    // is a count and not one, and quietly dragged the wrong items in.
    const auto intoGrid = [this](ItemId item, u32 amount,
                                 std::initializer_list<usize> cells) {
        m_player.inventory.add(item, amount);
        m_screen->click(m_screen->containerSlots()); // The pack's slot 0.
        for (const usize cell : cells) {
            m_screen->split(cell);
        }
    };
    // A stone pickaxe, mid-recipe: cobblestone across the top, sticks down the
    // middle. An empty grid proves the nine cells draw and nothing else, and the
    // output preview is the part with a recipe match behind it.
    intoGrid(itemIdOf("cobblestone"), 3, {0, 1, 2});
    intoGrid(itemIdOf("stick"), 2, {4, 7});

    // Something to look at. A capture of an empty pack proves the panel draws
    // and nothing else -- not the icons, not the counts, not the two-digit
    // layout, which are the parts with arithmetic in them.
    m_player.inventory.add(blockIdOf("stone"), 64);
    m_player.inventory.add(blockIdOf("dirt"), 7);
    m_player.inventory.add(blockIdOf("grass"), 12);
    m_player.inventory.add(blockIdOf("sand"), 3);
    m_player.inventory.add(blockIdOf("oak_log"), 128);
    m_player.inventory.add(blockIdOf("cobblestone"), 45);
    m_player.inventory.add(blockIdOf("gravel"), 1);

    // Phase 16's half: items that are not blocks, and one tool of each kind, so a
    // capture shows every icon recipe rather than only the cubes. Phase 17 adds
    // the table itself, which is a block whose icon is the thing you go and place.
    m_player.inventory.add(itemIdOf("coal"), 9);
    m_player.inventory.add(itemIdOf("stick"), 6);
    m_player.inventory.add(itemIdOf("wooden_pickaxe"), 1);
    m_player.inventory.add(itemIdOf("stone_axe"), 1);
    m_player.inventory.add(itemIdOf("stone_sword"), 1);
    m_player.inventory.add(itemIdOf("wooden_shovel"), 1);
    m_player.inventory.add(blockIdOf("crafting_table"), 2);
    // Phase 10's half: a block that is not a cube. A capture of the pack should show
    // one, and a build handed to somebody to play needs enough of them to be a floor.
    m_player.inventory.add(blockIdOf("oak_slab"), 64);

    m_player.health = 13.0f; // An odd number, so a half heart is in the frame too.
}

void Engine::seedHeldItem() {
    if (m_options.heldItem.empty()) {
        return;
    }

    const ItemId item = itemIdOrNothing(m_options.heldItem);
    MC_VERIFY_MSG(item != kNoItem, "--hold names an item that does not exist");

    // Into the first hotbar slot, which is the one selected at startup. Placed
    // rather than added, so it is held whatever else the seeding above put there.
    //
    // **A whole stack of whatever stacks, and one of whatever does not.** It used to be
    // a flat 1, which is right for the captures this flag was written for -- a tool in
    // the fist -- and useless for the thing it is now also used for, handing a build to
    // somebody to play. One slab cannot be made into a floor. `stackLimit()` already
    // knows the difference, so a pickaxe stays a pickaxe and a block arrives as 64.
    ItemStack stack{item, 1};
    stack.count = stack.stackLimit();
    m_player.inventory.mutableAt(0) = stack;
    m_player.hotbarSlot = 0;
    logInfo("Holding: {} x{}", itemName(item), stack.count);
}

} // namespace mc
