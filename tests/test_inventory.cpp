#include "render/ScreenLayout.hpp"
#include "world/BlockTable.hpp"
#include "world/CraftingGrid.hpp"
#include "world/Inventory.hpp"
#include "world/Screen.hpp"

#include <doctest/doctest.h>

using namespace mc;

namespace {

/// The player's own window: a 2x2 grid, its output, then the thirty-six slots.
///
/// **Held together rather than returned**, because a `Screen` points at both and a
/// factory returning one would hand back a screen over two destroyed containers.
struct PlayerScreen {
    Inventory inventory;
    CraftingGrid craft{2};
    Screen screen{inventory, craft};

    /// The flat index of the player's own slot `n`. Container slots come first, so
    /// this is the offset every storage assertion below is written against.
    usize player(usize n) const { return screen.containerSlots() + n; }
};

} // namespace

TEST_CASE("adding fills partial stacks before empty slots") {
    Inventory inventory;

    CHECK(inventory.add(kStoneBlock, 10) == 0);
    CHECK(inventory.at(0).item == kStoneBlock);
    CHECK(inventory.at(0).count == 10);
    CHECK(inventory.usedSlots() == 1);

    // Topping up must not open a second slot while the first has room. A player who
    // mines fifty stone should have one stack, not fifty entries.
    CHECK(inventory.add(kStoneBlock, 20) == 0);
    CHECK(inventory.at(0).count == 30);
    CHECK(inventory.usedSlots() == 1);

    // Past the stack limit it spills into the next slot, and only then.
    CHECK(inventory.add(kStoneBlock, 50) == 0);
    CHECK(inventory.at(0).count == Inventory::kMaxStack);
    CHECK(inventory.at(1).count == 16);
    CHECK(inventory.count(kStoneBlock) == 80);
}

TEST_CASE("the hotbar fills before the main grid") {
    Inventory inventory;

    // Nine different block types land in slots 0-8, which is what makes a freshly
    // collected block reachable without opening anything.
    const BlockId types[]{kStoneBlock,  kDirtBlock,     kGrassBlock,
                          kSandBlock,   kGravelBlock,   kOakLogBlock,
                          kOakLeavesBlock, kDeepslateBlock, kBedrockBlock};
    for (usize i = 0; i < 9; ++i) {
        CHECK(inventory.add(types[i], 1) == 0);
        CHECK(inventory.at(i).item == types[i]);
    }
    CHECK(inventory.usedSlots() == Inventory::kHotbarSlots);
}

TEST_CASE("an inventory can fill up, and says so") {
    PlayerScreen ui;

    // Every slot to the brim with one type. `add` returns what did not fit rather
    // than pretending it did -- which is what lets a picked-up stack stay on the
    // ground instead of being deleted.
    //
    // **The crafting grid is not somewhere `add` can reach**, and since Phase 17 it
    // is not even in the same object: a picked-up stack landing in a crafting cell
    // would silently change what the grid produces, and now it cannot be expressed.
    const u32 capacity = Inventory::kStorageSlots * Inventory::kMaxStack;
    CHECK(ui.inventory.add(kStoneBlock, capacity) == 0);
    CHECK(ui.inventory.count(kStoneBlock) == capacity);
    CHECK(ui.inventory.usedSlots() == Inventory::kStorageSlots);
    CHECK(ui.screen.at(0).empty()); // Container slot 0: the first crafting cell.

    CHECK(ui.inventory.add(kStoneBlock, 5) == 5);
    CHECK(ui.inventory.add(kDirtBlock, 3) == 3);
    CHECK(ui.inventory.count(kDirtBlock) == 0);
}

TEST_CASE("air is not a thing you can carry") {
    Inventory inventory;
    CHECK(inventory.add(kAirBlock, 10) == 10);
    CHECK(inventory.count(kAirBlock) == 0);
    CHECK(inventory.usedSlots() == 0);
}

TEST_CASE("taking one empties the slot at zero") {
    Inventory inventory;
    inventory.add(kStoneBlock, 2);

    CHECK(inventory.takeOne(0));
    CHECK(inventory.at(0).count == 1);
    CHECK(inventory.takeOne(0));
    CHECK(inventory.at(0).empty());
    // An emptied slot must clear its block too, or the hotbar would keep drawing an
    // icon for something that is gone.
    CHECK(inventory.at(0).item == kAirBlock);

    CHECK_FALSE(inventory.takeOne(0));
    CHECK_FALSE(inventory.takeOne(Inventory::kStorageSlots)); // Out of range.
}

TEST_CASE("clicking picks a stack up and puts it down") {
    PlayerScreen ui;
    ui.inventory.add(kStoneBlock, 10);

    ui.screen.click(ui.player(0));
    CHECK(ui.inventory.at(0).empty());
    CHECK(ui.inventory.cursor().item == kStoneBlock);
    CHECK(ui.inventory.cursor().count == 10);

    ui.screen.click(ui.player(20));
    CHECK(ui.inventory.cursorEmpty());
    CHECK(ui.inventory.at(20).count == 10);

    // Clicking an empty slot with an empty hand is a click that does nothing rather
    // than one that puts air somewhere.
    ui.screen.click(ui.player(5));
    CHECK(ui.inventory.cursorEmpty());
    CHECK(ui.inventory.at(5).empty());
}

TEST_CASE("clicking the same type pours rather than swapping") {
    PlayerScreen ui;
    ui.inventory.add(kStoneBlock, 10);

    // Built with the cursor rather than with a second `add`, because `add` tops up
    // an existing partial stack instead of opening a new one -- which is the rule
    // the first test in this file pins, and which would otherwise quietly make this
    // test set up something other than what it says.
    ui.screen.click(ui.player(0));
    ui.screen.split(ui.player(20)); // one down, nine still in hand
    REQUIRE(ui.inventory.at(20).count == 1);
    REQUIRE(ui.inventory.cursor().count == 9);

    ui.screen.click(ui.player(20));
    CHECK(ui.inventory.cursorEmpty());
    CHECK(ui.inventory.at(20).count == 10);
}

TEST_CASE("clicking a different type swaps the two") {
    PlayerScreen ui;
    ui.inventory.add(kStoneBlock, 10);
    ui.inventory.add(kDirtBlock, 4);

    ui.screen.click(ui.player(0));
    REQUIRE(ui.inventory.cursor().item == kStoneBlock);

    ui.screen.click(ui.player(1));
    CHECK(ui.inventory.at(1).item == kStoneBlock);
    CHECK(ui.inventory.at(1).count == 10);
    CHECK(ui.inventory.cursor().item == kDirtBlock);
    CHECK(ui.inventory.cursor().count == 4);
}

TEST_CASE("merging into a nearly full stack keeps the remainder in hand") {
    PlayerScreen ui;
    ui.inventory.add(kStoneBlock, Inventory::kMaxStack); // slot 0, full
    ui.inventory.add(kStoneBlock, 60);                   // slot 1

    ui.screen.click(ui.player(1));
    CHECK(ui.inventory.cursor().count == 60);

    // Slot 0 is full, so this swaps rather than pouring. Same type, no room: the
    // useful behaviour is that nothing is lost either way.
    ui.screen.click(ui.player(0));
    CHECK(ui.inventory.at(0).count + ui.inventory.cursor().count
          == 60 + Inventory::kMaxStack);
}

TEST_CASE("right click splits a stack and places one at a time") {
    PlayerScreen ui;
    ui.inventory.add(kStoneBlock, 9);

    // The larger half stays in hand, which is vanilla's rounding.
    ui.screen.split(ui.player(0));
    CHECK(ui.inventory.cursor().count == 5);
    CHECK(ui.inventory.at(0).count == 4);

    // With something in hand, a right click puts down exactly one.
    ui.screen.split(ui.player(10));
    CHECK(ui.inventory.at(10).count == 1);
    CHECK(ui.inventory.cursor().count == 4);

    ui.screen.split(ui.player(10));
    CHECK(ui.inventory.at(10).count == 2);
    CHECK(ui.inventory.cursor().count == 3);

    // A slot holding something else takes nothing.
    ui.inventory.add(kDirtBlock, 1); // slot 1
    ui.screen.split(ui.player(1));
    CHECK(ui.inventory.at(1).item == kDirtBlock);
    CHECK(ui.inventory.at(1).count == 1);
    CHECK(ui.inventory.cursor().count == 3);
}

TEST_CASE("closing with a full hand does not delete it") {
    PlayerScreen ui;
    ui.inventory.add(kStoneBlock, 10);
    ui.screen.click(ui.player(0));
    REQUIRE(ui.inventory.cursor().count == 10);

    // Back into the inventory, nothing left over.
    const Screen::Release step = ui.screen.releaseOne();
    CHECK(step.moved);
    CHECK(step.spilled.empty());
    CHECK(ui.inventory.cursorEmpty());
    CHECK(ui.inventory.count(kStoneBlock) == 10);
}

TEST_CASE("closing with a full inventory hands the remainder back to be dropped") {
    PlayerScreen ui;
    const u32 capacity = Inventory::kStorageSlots * Inventory::kMaxStack;
    ui.inventory.add(kStoneBlock, capacity);

    // Pick a full stack up, then fill the hole it left.
    ui.screen.click(ui.player(0));
    REQUIRE(ui.inventory.cursor().count == Inventory::kMaxStack);
    ui.inventory.add(kDirtBlock, Inventory::kMaxStack);

    // There is now nowhere for the held stack to go, and it must come back to the
    // caller rather than evaporating because a window closed.
    const Screen::Release step = ui.screen.releaseOne();
    CHECK(step.moved);
    CHECK(step.spilled.item == kStoneBlock);
    CHECK(step.spilled.count == Inventory::kMaxStack);
    CHECK(ui.inventory.cursorEmpty());
}

TEST_CASE("closing gives the crafting grid back before it is lost") {
    PlayerScreen ui;
    ui.inventory.add(itemIdOf("oak_planks"), 4);

    // Into the grid the way a player does it: pick the stack up, right click a cell.
    ui.screen.click(ui.player(0));
    ui.screen.split(0);
    ui.screen.split(1);
    REQUIRE(ui.screen.at(0).count == 1);
    REQUIRE(ui.inventory.cursor().count == 2);

    // Close: the hand first, then every filled cell. Nothing may be left behind.
    while (ui.screen.releaseOne().moved) {
    }
    CHECK(ui.inventory.cursorEmpty());
    CHECK(ui.screen.at(0).empty());
    CHECK(ui.screen.at(1).empty());
    CHECK(ui.inventory.count(itemIdOf("oak_planks")) == 4);
}

TEST_CASE("every slot has a distinct rectangle and hit tests back to itself") {
    // **The property this whole class exists for.** If drawing and clicking computed
    // slot rectangles separately they would agree until someone changed a margin.
    for (const ScreenKind kind : {ScreenKind::Player, ScreenKind::CraftingTable}) {
        for (const f32 aspect : {16.0f / 9.0f, 1.0f, 4.0f / 3.0f, 21.0f / 9.0f}) {
            const ScreenLayout layout{aspect, kind};

            for (usize i = 0; i < layout.slotCount(); ++i) {
                const UiRect rect = layout.slot(i);
                const f32 midX = (rect.x0 + rect.x1) * 0.5f;
                const f32 midY = (rect.y0 + rect.y1) * 0.5f;

                const auto hit = layout.hitTest(midX, midY);
                REQUIRE(hit.has_value());
                CHECK(*hit == i);

                // And it is inside the panel, or the window would be drawn around a
                // slot that sits outside it.
                CHECK(layout.panel().contains(midX, midY));
            }
        }
    }
}

TEST_CASE("the table's window has five more slots than the player's own") {
    // Vanilla's gate, as arithmetic: 3x3 against 2x2 is the whole difference, and it
    // is why a pickaxe needs a table.
    const ScreenLayout player{16.0f / 9.0f, ScreenKind::Player};
    const ScreenLayout table{16.0f / 9.0f, ScreenKind::CraftingTable};

    CHECK(player.containerSlots() == 5); // 2x2 and an output
    CHECK(table.containerSlots() == 10); // 3x3 and an output
    CHECK(table.slotCount() == player.slotCount() + 5);
}

TEST_CASE("slots are square whatever the window shape") {
    for (const f32 aspect : {16.0f / 9.0f, 1.0f, 21.0f / 9.0f}) {
        const ScreenLayout layout{aspect, ScreenKind::Player};
        const UiRect rect = layout.slot(0);

        // NDC x is compressed by the aspect, so a square slot is `aspect` times
        // wider in NDC than it is tall. Getting this wrong is what makes a hotbar
        // come out as rectangles on an ultrawide monitor.
        const f32 width = (rect.x1 - rect.x0) * aspect;
        const f32 height = rect.y1 - rect.y0;
        CHECK(width == doctest::Approx(height).epsilon(0.001));
    }
}

TEST_CASE("a point outside every slot hits nothing") {
    const ScreenLayout layout{16.0f / 9.0f, ScreenKind::Player};

    CHECK_FALSE(layout.hitTest(-0.999f, 0.999f).has_value());
    CHECK_FALSE(layout.hitTest(0.0f, 0.999f).has_value());

    // The gap between the main grid and the hotbar is a real gap: a click there is
    // not a click on either, which is what makes dropping-outside work.
    const UiRect lastGrid = layout.slot(layout.slotCount() - 1);
    const UiRect hotbar = layout.slot(layout.containerSlots());
    const f32 between = (lastGrid.y0 + hotbar.y1) * 0.5f;
    CHECK_FALSE(layout.hitTest(0.0f, between).has_value());
}
