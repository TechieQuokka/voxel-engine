#include "render/InventoryLayout.hpp"
#include "world/BlockTable.hpp"
#include "world/Inventory.hpp"

#include <doctest/doctest.h>

using namespace mc;

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
    Inventory inventory;

    // Every *storage* slot to the brim with one type. `add` returns what did not fit
    // rather than pretending it did -- which is what lets a picked-up stack stay on
    // the ground instead of being deleted.
    //
    // **`kStorageSlots`, not `kSlotCount`.** Since the crafting grid landed the slot
    // index space runs past storage into the grid and the output, and a full pack is
    // 36 slots rather than 46. `add` must never reach the grid: a picked-up stack
    // landing in a crafting cell would silently change what the grid produces.
    const u32 capacity = Inventory::kStorageSlots * Inventory::kMaxStack;
    CHECK(inventory.add(kStoneBlock, capacity) == 0);
    CHECK(inventory.count(kStoneBlock) == capacity);
    CHECK(inventory.usedSlots() == Inventory::kStorageSlots);
    CHECK(inventory.at(Inventory::kFirstCraftSlot).empty());

    CHECK(inventory.add(kStoneBlock, 5) == 5);
    CHECK(inventory.add(kDirtBlock, 3) == 3);
    CHECK(inventory.count(kDirtBlock) == 0);
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
    CHECK_FALSE(inventory.takeOne(Inventory::kSlotCount)); // Out of range.
}

TEST_CASE("clicking picks a stack up and puts it down") {
    Inventory inventory;
    inventory.add(kStoneBlock, 10);

    inventory.clickSlot(0);
    CHECK(inventory.at(0).empty());
    CHECK(inventory.cursor().item == kStoneBlock);
    CHECK(inventory.cursor().count == 10);

    inventory.clickSlot(20);
    CHECK(inventory.cursorEmpty());
    CHECK(inventory.at(20).count == 10);

    // Clicking an empty slot with an empty hand is a click that does nothing rather
    // than one that puts air somewhere.
    inventory.clickSlot(5);
    CHECK(inventory.cursorEmpty());
    CHECK(inventory.at(5).empty());
}

TEST_CASE("clicking the same type pours rather than swapping") {
    Inventory inventory;
    inventory.add(kStoneBlock, 10);

    // Built with the cursor rather than with a second `add`, because `add` tops up
    // an existing partial stack instead of opening a new one -- which is the rule
    // the first test in this file pins, and which would otherwise quietly make this
    // test set up something other than what it says.
    inventory.clickSlot(0);
    inventory.splitSlot(20); // one down, nine still in hand
    REQUIRE(inventory.at(20).count == 1);
    REQUIRE(inventory.cursor().count == 9);

    inventory.clickSlot(20);
    CHECK(inventory.cursorEmpty());
    CHECK(inventory.at(20).count == 10);
}

TEST_CASE("clicking a different type swaps the two") {
    Inventory inventory;
    inventory.add(kStoneBlock, 10);
    inventory.add(kDirtBlock, 4);

    inventory.clickSlot(0);
    REQUIRE(inventory.cursor().item == kStoneBlock);

    inventory.clickSlot(1);
    CHECK(inventory.at(1).item == kStoneBlock);
    CHECK(inventory.at(1).count == 10);
    CHECK(inventory.cursor().item == kDirtBlock);
    CHECK(inventory.cursor().count == 4);
}

TEST_CASE("merging into a nearly full stack keeps the remainder in hand") {
    Inventory inventory;
    inventory.add(kStoneBlock, Inventory::kMaxStack); // slot 0, full
    inventory.add(kStoneBlock, 60);                   // slot 1

    inventory.clickSlot(1);
    CHECK(inventory.cursor().count == 60);

    // Slot 0 is full, so this swaps rather than pouring. Same type, no room: the
    // useful behaviour is that nothing is lost either way.
    inventory.clickSlot(0);
    CHECK(inventory.at(0).count + inventory.cursor().count == 60 + Inventory::kMaxStack);
}

TEST_CASE("right click splits a stack and places one at a time") {
    Inventory inventory;
    inventory.add(kStoneBlock, 9);

    // The larger half stays in hand, which is vanilla's rounding.
    inventory.splitSlot(0);
    CHECK(inventory.cursor().count == 5);
    CHECK(inventory.at(0).count == 4);

    // With something in hand, a right click puts down exactly one.
    inventory.splitSlot(10);
    CHECK(inventory.at(10).count == 1);
    CHECK(inventory.cursor().count == 4);

    inventory.splitSlot(10);
    CHECK(inventory.at(10).count == 2);
    CHECK(inventory.cursor().count == 3);

    // A slot holding something else takes nothing.
    inventory.add(kDirtBlock, 1); // slot 1
    inventory.splitSlot(1);
    CHECK(inventory.at(1).item == kDirtBlock);
    CHECK(inventory.at(1).count == 1);
    CHECK(inventory.cursor().count == 3);
}

TEST_CASE("closing with a full hand does not delete it") {
    Inventory inventory;
    inventory.add(kStoneBlock, 10);
    inventory.clickSlot(0);
    REQUIRE(inventory.cursor().count == 10);

    // Back into the inventory, nothing left over.
    const ItemStack leftover = inventory.releaseCursor();
    CHECK(leftover.empty());
    CHECK(inventory.cursorEmpty());
    CHECK(inventory.count(kStoneBlock) == 10);
}

TEST_CASE("closing with a full inventory hands the remainder back to be dropped") {
    Inventory inventory;
    const u32 capacity = Inventory::kSlotCount * Inventory::kMaxStack;
    inventory.add(kStoneBlock, capacity);

    // Pick a full stack up, then fill the hole it left.
    inventory.clickSlot(0);
    REQUIRE(inventory.cursor().count == Inventory::kMaxStack);
    inventory.add(kDirtBlock, Inventory::kMaxStack);

    // There is now nowhere for the held stack to go, and it must come back to the
    // caller rather than evaporating because a window closed.
    const ItemStack leftover = inventory.releaseCursor();
    CHECK(leftover.item == kStoneBlock);
    CHECK(leftover.count == Inventory::kMaxStack);
    CHECK(inventory.cursorEmpty());
}

TEST_CASE("every slot has a distinct rectangle and hit tests back to itself") {
    // **The property this whole class exists for.** If drawing and clicking computed
    // slot rectangles separately they would agree until someone changed a margin.
    for (const f32 aspect : {16.0f / 9.0f, 1.0f, 4.0f / 3.0f, 21.0f / 9.0f}) {
        const InventoryLayout layout{aspect};

        for (usize i = 0; i < Inventory::kSlotCount; ++i) {
            const UiRect rect = layout.slot(i);
            const f32 midX = (rect.x0 + rect.x1) * 0.5f;
            const f32 midY = (rect.y0 + rect.y1) * 0.5f;

            const auto hit = layout.hitTest(midX, midY);
            REQUIRE(hit.has_value());
            CHECK(*hit == i);

            // And it is inside the panel, or the window would be drawn around a slot
            // that sits outside it.
            CHECK(layout.panel().contains(midX, midY));
        }
    }
}

TEST_CASE("slots are square whatever the window shape") {
    for (const f32 aspect : {16.0f / 9.0f, 1.0f, 21.0f / 9.0f}) {
        const InventoryLayout layout{aspect};
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
    const InventoryLayout layout{16.0f / 9.0f};

    CHECK_FALSE(layout.hitTest(-0.999f, 0.999f).has_value());
    CHECK_FALSE(layout.hitTest(0.0f, 0.999f).has_value());

    // The gap between the grid and the hotbar is a real gap: a click there is not a
    // click on either, which is what makes dropping-outside work.
    const UiRect lastGrid = layout.slot(Inventory::kSlotCount - 1);
    const UiRect hotbar = layout.slot(0);
    const f32 between = (lastGrid.y0 + hotbar.y1) * 0.5f;
    CHECK_FALSE(layout.hitTest(0.0f, between).has_value());
}
