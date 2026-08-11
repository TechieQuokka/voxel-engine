#include "world/BlockTable.hpp"

#include <doctest/doctest.h>

using namespace mc;

TEST_CASE("break time follows vanilla hardness") {
    // hardness * 1.5, the harvestable branch of vanilla's formula. See breakSeconds
    // for why this engine always takes that branch.
    CHECK(breakSeconds(blockIdOf("dirt")) == doctest::Approx(0.75f));
    CHECK(breakSeconds(blockIdOf("grass")) == doctest::Approx(0.9f));
    CHECK(breakSeconds(blockIdOf("sand")) == doctest::Approx(0.75f));
    CHECK(breakSeconds(blockIdOf("stone")) == doctest::Approx(2.25f));
    CHECK(breakSeconds(blockIdOf("deepslate")) == doctest::Approx(4.5f));
    CHECK(breakSeconds(blockIdOf("oak_leaves")) == doctest::Approx(0.3f));
    CHECK(breakSeconds(blockIdOf("oak_log")) == doctest::Approx(3.0f));
}

TEST_CASE("bedrock is the only unbreakable block") {
    CHECK(isUnbreakable(kBedrockBlock));
    // And it reports no break time rather than a negative one, so a caller that
    // forgets to ask cannot end up dividing by it.
    CHECK(breakSeconds(kBedrockBlock) == doctest::Approx(0.0f));

    usize unbreakable = 0;
    for (usize id = 0; id < kBlocks.size(); ++id) {
        if (isUnbreakable(static_cast<BlockId>(id))) {
            ++unbreakable;
        }
    }
    CHECK(unbreakable == 1);
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
