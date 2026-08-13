#include "world/BlockTable.hpp"
#include "world/ItemTable.hpp"

#include <doctest/doctest.h>

using namespace mc;

TEST_CASE("break time follows vanilla hardness") {
    // hardness * 1.5, the harvestable branch of vanilla's formula. Every block here
    // is one a bare hand can harvest, so the branch is the same one this test checked
    // before tools existed and the numbers are unchanged.
    CHECK(breakSeconds(blockIdOf("dirt")) == doctest::Approx(0.75f));
    CHECK(breakSeconds(blockIdOf("grass")) == doctest::Approx(0.9f));
    CHECK(breakSeconds(blockIdOf("sand")) == doctest::Approx(0.75f));
    CHECK(breakSeconds(blockIdOf("oak_leaves")) == doctest::Approx(0.3f));
    CHECK(breakSeconds(blockIdOf("oak_log")) == doctest::Approx(3.0f));
}

TEST_CASE("rock takes the no-harvest branch bare-handed") {
    // **This is the case that changed in Phase 16**, and it is the whole reason a
    // pickaxe is worth making. Stone used to report 2.25 s because the engine took
    // the harvestable branch for everything; a bare hand cannot harvest stone, so
    // vanilla's formula divides by 100 rather than 30 and the answer is hardness * 5.
    CHECK(breakSeconds(blockIdOf("stone")) == doctest::Approx(7.5f));
    CHECK(breakSeconds(blockIdOf("deepslate")) == doctest::Approx(15.0f));

    // And the hardness numbers themselves did not move, which is what the old note
    // on breakSeconds predicted when it said tools would arrive as a multiplier.
    CHECK(kBlocks[blockIdOf("stone")].hardness == doctest::Approx(1.5f));
    CHECK(kBlocks[blockIdOf("deepslate")].hardness == doctest::Approx(3.0f));
}

TEST_CASE("a matching tool divides the time and an unmatched one does not") {
    const BlockId stone = blockIdOf("stone");

    // Wood is speed 2, stone speed 4, against the harvestable divisor of 30/20.
    CHECK(breakSeconds(stone, itemIdOf("wooden_pickaxe")) == doctest::Approx(1.125f));
    CHECK(breakSeconds(stone, itemIdOf("stone_pickaxe")) == doctest::Approx(0.5625f));

    // **A shovel is not a pickaxe.** It neither speeds stone up nor harvests it, so
    // it is exactly as good as a fist -- which is what stops one good tool being the
    // answer to everything.
    CHECK(breakSeconds(stone, itemIdOf("stone_shovel")) == doctest::Approx(7.5f));

    // Dirt has a tool but no tier: a shovel is faster, a fist still works.
    const BlockId dirt = blockIdOf("dirt");
    CHECK(breakSeconds(dirt, itemIdOf("wooden_shovel")) == doctest::Approx(0.375f));
    CHECK(breakSeconds(dirt) == doctest::Approx(0.75f));
}

TEST_CASE("harvest gating is what makes a pickaxe necessary") {
    const BlockId stone = blockIdOf("stone");

    // Bare-handed stone breaks and yields nothing. That is the message the player
    // receives, and it is a drop rule rather than a refusal to break.
    CHECK_FALSE(canHarvest(stone, kNoItem));
    CHECK(dropOf(stone, kNoItem) == kNoItem);

    CHECK(canHarvest(stone, itemIdOf("wooden_pickaxe")));
    CHECK(dropOf(stone, itemIdOf("wooden_pickaxe")) == itemIdOf("cobblestone"));

    // A wooden pickaxe is not enough for iron, and iron ore is therefore the wall
    // Phase 16 deliberately stops at -- an iron tool needs smelting, which is a
    // furnace, which is a second window.
    const BlockId ironOre = blockIdOf("iron_ore");
    CHECK_FALSE(canHarvest(ironOre, itemIdOf("wooden_pickaxe")));
    CHECK(canHarvest(ironOre, itemIdOf("stone_pickaxe")));

    // Diamond needs iron, which cannot be crafted yet. Reaching it is Phase 17's
    // exit criterion arriving early if this ever starts passing with a stone tool.
    CHECK_FALSE(canHarvest(blockIdOf("diamond_ore"), itemIdOf("stone_pickaxe")));

    // Dirt has no tier, so anything harvests it including nothing at all.
    CHECK(canHarvest(blockIdOf("dirt"), kNoItem));
    CHECK(dropOf(blockIdOf("dirt"), kNoItem) == itemIdOf("dirt"));
}

TEST_CASE("an ore can drop something that is not a block") {
    // **The shortest statement of what Phase 16 changed.** Before the split a coal
    // ore dropped a coal ore block, because a block was the only thing a drop could
    // be. `coal` has no BlockId at all.
    const ItemId coal = dropOf(blockIdOf("coal_ore"), itemIdOf("wooden_pickaxe"));
    CHECK(coal == itemIdOf("coal"));
    CHECK_FALSE(itemIsBlock(coal));
    CHECK(blockOfItem(coal) == kAirBlock);

    // The deepslate variant drops the same item, not a deepslate-flavoured one.
    CHECK(dropOf(blockIdOf("deepslate_coal_ore"), itemIdOf("wooden_pickaxe")) == coal);

    // Stone still drops a block, and that block is placeable.
    const ItemId cobble = dropOf(blockIdOf("stone"), itemIdOf("wooden_pickaxe"));
    CHECK(itemIsBlock(cobble));
    CHECK(blockOfItem(cobble) == blockIdOf("cobblestone"));
}

TEST_CASE("bedrock and water are the only unbreakable blocks") {
    CHECK(isUnbreakable(kBedrockBlock));
    // And it reports no break time rather than a negative one, so a caller that
    // forgets to ask cannot end up dividing by it.
    CHECK(breakSeconds(kBedrockBlock) == doctest::Approx(0.0f));

    // Water joined bedrock when it landed. It is unbreakable for a different reason
    // -- vanilla needs a bucket, and this engine has no items -- and it is also
    // unreachable, because the aim ray passes straight through a fluid and never
    // offers it as a target. Both belts, one brace.
    CHECK(isUnbreakable(kWaterBlock));
    CHECK(breakSeconds(kWaterBlock) == doctest::Approx(0.0f));

    // **Bedrock is the only unbreakable block that is not a fluid**, which is the
    // invariant worth pinning. Counting to a literal broke the moment water grew
    // seven flow levels, and it would have broken again for lava -- the number was
    // never the point.
    usize unbreakableSolids = 0;
    for (usize id = 0; id < kBlocks.size(); ++id) {
        const auto block = static_cast<BlockId>(id);
        if (isUnbreakable(block) && !isFluid(block)) {
            ++unbreakableSolids;
        }
    }
    CHECK(unbreakableSolids == 1);

    // And every fluid is unbreakable, at every level. A flow level that could be
    // punched out would leave a hole in a river that nothing refills.
    for (usize id = 0; id < kBlocks.size(); ++id) {
        const auto block = static_cast<BlockId>(id);
        CAPTURE(kBlocks[id].name);
        CHECK((!isFluid(block) || isUnbreakable(block)));
    }
}

TEST_CASE("fluid and solid are different questions from opaque") {
    // Water is the block that forces the distinction: not opaque, so it hides
    // nothing behind it; not solid, so it holds nothing up. Every physics caller
    // wants `isSolidBlock` and every mesher caller wants `opaque`, and before water
    // existed `!= kAirBlock` was indistinguishable from the first.
    CHECK(isFluid(kWaterBlock));
    CHECK_FALSE(kBlocks[kWaterBlock].opaque);
    CHECK_FALSE(isSolidBlock(kWaterBlock));

    CHECK_FALSE(isSolidBlock(kAirBlock));
    CHECK_FALSE(isFluid(kAirBlock));

    // Everything else is solid and opaque, including leaves -- which are a full
    // cube here on purpose, the way vanilla's fast graphics draws them.
    CHECK(isSolidBlock(kStoneBlock));
    CHECK(isSolidBlock(kOakLeavesBlock));

    // Water is the only fluid, in nine block types: a source, a falling block, and
    // seven flowing levels.
    usize fluids = 0;
    for (usize id = 0; id < kBlocks.size(); ++id) {
        fluids += isFluid(static_cast<BlockId>(id)) ? 1u : 0u;
    }
    CHECK(fluids == kMaxFluidLevel + 2u);

    // Every level resolves to exactly the block that claims it, and nothing beyond
    // level 7 exists -- vanilla turns that into air rather than a tenth block.
    for (u8 level = 0; level <= kMaxFluidLevel; ++level) {
        const BlockId water = waterAtLevel(level);
        CAPTURE(level);
        CHECK(isFluid(water));
        CHECK(fluidLevelOf(water) == level);
        CHECK_FALSE(isSolidBlock(water));
        CHECK(isFluidReplaceable(water));
    }
    CHECK(waterAtLevel(kMaxFluidLevel + 1u) == kAirBlock);
}

TEST_CASE("a source and a falling block are both full strength and only one persists") {
    // **`waterAtLevel(0)` is the falling block, not the source, and that is the point
    // of the distinction.** Both are level 0, so both spread the full seven blocks --
    // vanilla resets the depth at each new elevation, which is why a waterfall reaches
    // as far as a lake edge does. But a flow must never *manufacture* a source: a
    // source never drains, so one created by accident is a leak that fills the world.
    CHECK(waterAtLevel(0) != kWaterBlock);
    CHECK(fluidLevelOf(waterAtLevel(0)) == 0);
    CHECK(fluidLevelOf(kWaterBlock) == 0);

    CHECK(isFluidSource(kWaterBlock));
    CHECK_FALSE(isFluidSource(waterAtLevel(0)));
    for (u8 level = 1; level <= kMaxFluidLevel; ++level) {
        CAPTURE(level);
        CHECK_FALSE(isFluidSource(waterAtLevel(level)));
    }

    // Exactly one source block type. A second would be a second thing that never
    // drains, and the reason to notice is that nothing else in the engine would.
    usize sources = 0;
    for (usize id = 0; id < kBlocks.size(); ++id) {
        sources += isFluidSource(static_cast<BlockId>(id)) ? 1u : 0u;
    }
    CHECK(sources == 1);
}

TEST_CASE("every ore is harder in deepslate than in stone") {
    // Vanilla's rule, and the reason mining deep is slower: the rock is what is
    // hard, not the metal in it. 3.0 against 4.5 throughout.
    const std::string_view ores[] = {"coal", "iron", "copper", "gold",
                                     "redstone", "lapis", "diamond"};
    for (const std::string_view ore : ores) {
        f32 stoneHardness = 0.0f;
        f32 deepHardness = 0.0f;
        for (const BlockInfo& block : kBlocks) {
            if (block.name == ore) {
                continue;
            }
            if (block.name.find(ore) == std::string_view::npos
                || block.name.find("_ore") == std::string_view::npos) {
                continue;
            }
            if (block.name.starts_with("deepslate_")) {
                deepHardness = block.hardness;
            } else {
                stoneHardness = block.hardness;
            }
        }
        CAPTURE(ore);
        CHECK(stoneHardness == doctest::Approx(3.0f));
        CHECK(deepHardness == doctest::Approx(4.5f));
    }
}

TEST_CASE("every block that can be placed or generated has a hardness") {
    // Air is the one exception. A block left at the default 0 would break the
    // instant it was touched, which is the kind of thing that only shows up as
    // "why did that vanish" in play.
    for (usize id = 1; id < kBlocks.size(); ++id) {
        CAPTURE(kBlocks[id].name);
        CHECK(kBlocks[id].hardness != 0.0f);
    }
}
