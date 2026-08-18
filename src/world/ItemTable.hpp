#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"
#include "world/Tools.hpp"

#include <array>
#include <string_view>

namespace mc {

/// Everything that can sit in an inventory slot.
///
/// **Item ids extend block ids rather than replacing them**, and that one decision is
/// most of why Phase 16 was a change and not a rewrite. Ids `0 .. kBlocks.size()-1`
/// *are* `BlockId`s and mean "one of that block"; ids from `kFirstLooseItem` up index
/// `kItems` below and mean things that are not blocks at all.
///
/// The alternative was a parallel item table with one entry per block, which is the
/// obvious shape and is wrong here for a reason this codebase already knows: it would
/// be thirty index correspondences maintained by hand, and `BlockTable`'s whole
/// existence is the record of what that costs. **Adding a block is still one line and
/// gives it an item for free.** Adding a stick is one line here.
///
/// The cost is that `ItemId` and `BlockId` are the same underlying type and convert
/// silently, so the compiler cannot catch passing one where the other belongs.
/// `itemIsBlock` and `blockOfItem` are the seam, and the tests pin it.
using ItemId = u16;

/// Nothing. The same value as `kAirBlock`, deliberately -- an empty slot and an air
/// block are the same absence, and giving them different spellings would mean every
/// conversion had to remember to translate one into the other.
inline constexpr ItemId kNoItem = kAirBlock;

/// The first id that is *not* a block.
inline constexpr ItemId kFirstLooseItem = static_cast<ItemId>(kBlocks.size());

/// Per-item properties. Blocks do not appear here; they get theirs from `kBlocks`.
struct ItemInfo {
    std::string_view name;

    /// Layer in the block texture array. Item icons live in the same array -- see the
    /// note above the icon recipes in `BlockTable.hpp`.
    u16 icon = 0;

    ToolKind tool = ToolKind::None;
    ToolTier tier = ToolTier::None;

    /// **Tools stack to one, which is not decoration.** A slot holding sixty-four
    /// pickaxes is a slot that never runs out, and durability in Phase 17 has nowhere
    /// to live on a stack that is really sixty-four different tools.
    u32 maxStack = 64;
};

/// Items that are not blocks, in id order after every block.
///
/// **This table is short on purpose and will stay short.** Anything that can be
/// placed belongs in `kBlocks` and gets an item automatically; only things that
/// genuinely cannot be a block in the world go here.
inline constexpr std::array kItems{
    // Crafting intermediates. A stick is the canonical example of why this table has
    // to exist at all -- there is no arrangement of voxels that is a stick.
    ItemInfo{"stick", layerOf("stick")},

    // Ore drops. These are what makes the split visible in play rather than in a
    // header: before Phase 16 a coal ore dropped a coal *ore block*, because a block
    // was the only thing a drop could be.
    ItemInfo{"coal", layerOf("coal")},
    ItemInfo{"diamond", layerOf("diamond")},
    ItemInfo{"redstone", layerOf("redstone")},
    ItemInfo{"lapis_lazuli", layerOf("lapis_lazuli")},

    // Tools, in tier order. Wood and stone are craftable from what the world hands
    // you; iron and diamond are below and need the furnace.
    ItemInfo{"wooden_pickaxe", layerOf("wooden_pickaxe"), ToolKind::Pickaxe, ToolTier::Wood, 1},
    ItemInfo{"wooden_axe", layerOf("wooden_axe"), ToolKind::Axe, ToolTier::Wood, 1},
    ItemInfo{"wooden_shovel", layerOf("wooden_shovel"), ToolKind::Shovel, ToolTier::Wood, 1},
    ItemInfo{"wooden_sword", layerOf("wooden_sword"), ToolKind::Sword, ToolTier::Wood, 1},
    ItemInfo{"stone_pickaxe", layerOf("stone_pickaxe"), ToolKind::Pickaxe, ToolTier::Stone, 1},
    ItemInfo{"stone_axe", layerOf("stone_axe"), ToolKind::Axe, ToolTier::Stone, 1},
    ItemInfo{"stone_shovel", layerOf("stone_shovel"), ToolKind::Shovel, ToolTier::Stone, 1},
    ItemInfo{"stone_sword", layerOf("stone_sword"), ToolKind::Sword, ToolTier::Stone, 1},

    // **Smelted metal, which is what a furnace is for.** These cannot be blocks: a
    // bar of iron is a thing you carry, and the ore block it came from is a different
    // thing that is still in the wall.
    ItemInfo{"iron_ingot", layerOf("iron_ingot")},
    ItemInfo{"copper_ingot", layerOf("copper_ingot")},
    ItemInfo{"gold_ingot", layerOf("gold_ingot")},

    // Iron and diamond tools. **This is the far side of the wall Phase 16 stopped
    // at**: iron ore needs a stone pickaxe, an iron pickaxe needs a smelted ingot,
    // and diamond needs the iron pickaxe. Every step of that chain now exists.
    ItemInfo{"iron_pickaxe", layerOf("iron_pickaxe"), ToolKind::Pickaxe, ToolTier::Iron, 1},
    ItemInfo{"iron_axe", layerOf("iron_axe"), ToolKind::Axe, ToolTier::Iron, 1},
    ItemInfo{"iron_shovel", layerOf("iron_shovel"), ToolKind::Shovel, ToolTier::Iron, 1},
    ItemInfo{"iron_sword", layerOf("iron_sword"), ToolKind::Sword, ToolTier::Iron, 1},
    ItemInfo{"diamond_pickaxe", layerOf("diamond_pickaxe"), ToolKind::Pickaxe, ToolTier::Diamond, 1},
    ItemInfo{"diamond_axe", layerOf("diamond_axe"), ToolKind::Axe, ToolTier::Diamond, 1},
    ItemInfo{"diamond_shovel", layerOf("diamond_shovel"), ToolKind::Shovel, ToolTier::Diamond, 1},
    ItemInfo{"diamond_sword", layerOf("diamond_sword"), ToolKind::Sword, ToolTier::Diamond, 1},
};

inline constexpr ItemId kItemCount = static_cast<ItemId>(kFirstLooseItem + kItems.size());

/// True when this id names a block, so it can be placed.
constexpr bool itemIsBlock(ItemId id) {
    return id != kNoItem && id < kFirstLooseItem;
}

/// The block this item places, or `kAirBlock` when it places nothing.
constexpr BlockId blockOfItem(ItemId id) {
    return itemIsBlock(id) ? static_cast<BlockId>(id) : kAirBlock;
}

/// The block item for a block. The identity, and named anyway: a call site that says
/// `itemOfBlock(broken)` is stating that it knows the two spaces overlap, where a
/// bare assignment leaves the next reader to work it out.
constexpr ItemId itemOfBlock(BlockId id) { return static_cast<ItemId>(id); }

constexpr bool itemExists(ItemId id) { return id != kNoItem && id < kItemCount; }

/// The loose-item entry behind an id. Callers must have checked `itemExists` and
/// `!itemIsBlock`; this is the shared unchecked step the accessors below build on.
constexpr const ItemInfo& looseItem(ItemId id) {
    return kItems[static_cast<usize>(id) - kFirstLooseItem];
}

constexpr std::string_view itemName(ItemId id) {
    if (!itemExists(id)) {
        return kBlocks[kAirBlock].name;
    }
    return itemIsBlock(id) ? kBlocks[id].name : looseItem(id).name;
}

/// The texture layer an item draws with. A block item draws its **top** face, which
/// is the one that reads as the block at icon size -- a grass side at 16 pixels is
/// mostly dirt.
constexpr u16 itemIcon(ItemId id) {
    if (!itemExists(id)) {
        return 0;
    }
    return itemIsBlock(id) ? kBlocks[id].top : looseItem(id).icon;
}

constexpr u32 maxStackOf(ItemId id) {
    if (!itemExists(id)) {
        return 0;
    }
    return itemIsBlock(id) ? 64u : looseItem(id).maxStack;
}

constexpr ToolKind toolKindOf(ItemId id) {
    return itemExists(id) && !itemIsBlock(id) ? looseItem(id).tool : ToolKind::None;
}

constexpr ToolTier toolTierOf(ItemId id) {
    return itemExists(id) && !itemIsBlock(id) ? looseItem(id).tier : ToolTier::None;
}

/// Resolves a name to an id, searching blocks first and then items.
///
/// `consteval` for the same reason `blockIdOf` is: the whole benefit is that an
/// unknown name cannot survive to become a wrong item at runtime.
consteval ItemId itemIdOf(std::string_view name) {
    for (usize i = 0; i < kBlocks.size(); ++i) {
        if (kBlocks[i].name == name) {
            return static_cast<ItemId>(i);
        }
    }
    for (usize i = 0; i < kItems.size(); ++i) {
        if (kItems[i].name == name) {
            return static_cast<ItemId>(kFirstLooseItem + i);
        }
    }
    throw "unknown item name";
}

/// The runtime form of `itemIdOf`, for a name that came from data rather than from
/// source. Returns `kNoItem` for anything unmatched.
constexpr ItemId itemIdOrNothing(std::string_view name) {
    if (name.empty()) {
        return kNoItem;
    }
    for (usize i = 0; i < kBlocks.size(); ++i) {
        if (kBlocks[i].name == name) {
            return static_cast<ItemId>(i);
        }
    }
    for (usize i = 0; i < kItems.size(); ++i) {
        if (kItems[i].name == name) {
            return static_cast<ItemId>(kFirstLooseItem + i);
        }
    }
    return kNoItem;
}

// -- mining --------------------------------------------------------------------

/// Whether `held` is the right kind of tool for `block`, at a high enough tier to
/// yield a drop.
///
/// **A block with no `minTier` is harvestable by anything, including a fist.** That
/// is most of the world; the rule only bites on rock.
constexpr bool canHarvest(BlockId block, ItemId held) {
    const ToolTier required = minTierOf(block);
    if (required == ToolTier::None) {
        return true;
    }
    if (toolKindOf(held) != toolFor(block)) {
        return false;
    }
    return tierRank(toolTierOf(held)) >= tierRank(required);
}

/// The mining-speed multiplier `held` gives on `block`. 1.0 when the tool is wrong
/// for the job, which is also what a bare hand gives.
///
/// **The tool has to match the block's kind, not merely be a tool.** A pickaxe does
/// not dig dirt faster in vanilla and does not here; that is what stops one good tool
/// being the answer to everything.
constexpr f32 miningSpeed(BlockId block, ItemId held) {
    const ToolKind kind = toolFor(block);
    if (kind == ToolKind::None || toolKindOf(held) != kind) {
        return 1.0f;
    }
    return tierSpeed(toolTierOf(held));
}

/// How long `block` takes to break while holding `held`, in seconds. Zero for air and
/// for anything unbreakable -- callers are expected to ask `isUnbreakable` first.
///
/// Vanilla computes damage per tick as `speed / hardness / (canHarvest ? 30 : 100)`
/// and breaks the block when that reaches 1, which rearranges to the expression
/// below. At bare-hand speed 1.0 it is `hardness * 1.5` seconds when the block can be
/// harvested and `hardness * 5` when it cannot.
///
/// **The engine used to take the harvestable branch for everything, and Phase 16 is
/// where that stops.** The old note here said why it was right at the time: with no
/// tools the other branch is not "harder", it is a dead end, and bare-handed stone in
/// vanilla is 7.5 seconds for nothing at all. Tools are what turn a dead end into a
/// prerequisite. **None of the hardness numbers in `kBlocks` changed when they
/// landed**, which is what the old note predicted and is the reason this arrived as
/// one formula rather than as a re-tuning.
constexpr f32 breakSeconds(BlockId block, ItemId held = kNoItem) {
    if (block >= kBlocks.size() || isUnbreakable(block)) {
        return 0.0f;
    }
    const f32 divisor = canHarvest(block, held) ? 30.0f : 100.0f;
    return kBlocks[block].hardness * divisor / (miningSpeed(block, held) * 20.0f);
}

/// What breaking `block` with `held` yields. `kNoItem` means nothing drops.
///
/// **Two ways to get nothing, and they are different failures.** A block whose
/// `drops` is `"air"` yields nothing to anybody -- leaves. A block whose `minTier`
/// is not met yields nothing to *this* tool, and would have dropped for a better
/// one. The second is the message a player has to receive, and they receive it by
/// watching stone break and leave no cobblestone behind.
constexpr ItemId dropOf(BlockId block, ItemId held = kNoItem) {
    if (block >= kBlocks.size()) {
        return kNoItem;
    }
    if (!canHarvest(block, held)) {
        return kNoItem;
    }
    const std::string_view name = kBlocks[block].drops;
    if (name.empty()) {
        return itemOfBlock(block); // Drops itself, which is almost everything.
    }
    return itemIdOrNothing(name);
}

/// Proves every `drops` name in `kBlocks` matches a real block or item.
///
/// `dropOf` has to be runtime-callable and so cannot reject a bad name the way
/// `itemIdOf` does -- it returns nothing instead, which would silently mean "no
/// drop". This puts the compile error back, and it lives here rather than in
/// `BlockTable.hpp` because half the names it has to match are items.
consteval bool everyDropResolves() {
    for (const BlockInfo& block : kBlocks) {
        if (block.drops.empty()) {
            continue;
        }
        if (itemIdOrNothing(block.drops) == kNoItem && block.drops != "air") {
            return false;
        }
    }
    return true;
}

static_assert(everyDropResolves(),
              "a block's drops name matches neither a block in kBlocks nor an item in kItems");

static_assert(itemIdOf("stick") >= kFirstLooseItem,
              "loose items must be numbered after every block");
static_assert(itemIdOf("stone") == kStoneBlock,
              "a block's item id is its block id; the two spaces share their low half");
static_assert(!itemIsBlock(itemIdOf("coal")) && itemIsBlock(itemIdOf("coal_ore")),
              "coal is an item and coal ore is a block, which is the whole split");

} // namespace mc
