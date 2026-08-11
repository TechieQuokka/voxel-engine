#pragma once

#include "core/Types.hpp"

#include <array>
#include <string_view>

namespace mc {

/// Every block type the engine knows, and every texture layer they draw with.
///
/// **Adding a block is one entry in `kBlocks`.** That is the whole point of this
/// file. Before it, a block type was spread across four places -- a `TextureLayer`
/// enumerator, a `BlockId` constant, a row in a fixed-size array whose order had to
/// match those constants, and a hand-written call in the texture generator -- with
/// nothing but a comment holding the correspondence together. Two of those four
/// were index correspondences maintained by hand, and getting one wrong compiled,
/// linked, ran, and put the wrong texture on a block.
///
/// Two tables rather than one, because a block is not a texture. Grass draws three
/// layers and shares one of them (dirt) with another block type, so layers are a
/// deduplicated set that blocks refer to **by name**. Both tables are indexed by
/// position, so every index in the engine -- a BlockId, a texture array layer -- is
/// assigned by entry order and never written down. `blockIdOf` and `layerOf`
/// resolve a name during constant evaluation, so a typo is a compile error instead
/// of a wrong texture.

/// How a layer's pixels are produced. The renderer generates block textures
/// procedurally, so a layer carries a recipe rather than a file path.
enum class TextureRecipe : u8 {
    /// Uniform noise over a base colour, at two octaves. Stone, dirt and sand
    /// differ only in colour and how rough they are.
    Grain,
    /// Dirt with a ragged grass fringe along the top edge, so the side of a grass
    /// block reads correctly where it meets the top face.
    GrassSide,
    /// Grain in the host rock's colour, with a scatter of blobs in a second colour.
    /// Every ore is this, twice: once over stone and once over deepslate.
    Ore,
    /// Concentric growth rings in a second colour, for the cut end of a log. The
    /// one thing that makes a log read as a log rather than as a brown cube.
    Rings,
    /// Vertical streaks rather than isotropic noise, for bark. Grain would give a
    /// log the same texture as dirt, and the grain direction is most of what says
    /// which way the trunk runs.
    Bark,
};

struct LayerInfo {
    std::string_view name;
    TextureRecipe recipe = TextureRecipe::Grain;

    /// sRGB-encoded ARGB, which is what an authored PNG would be too. See
    /// DESIGN.md 6.9 for where the decode happens.
    u32 argb = 0xFFFFFFFFu;
    /// Second colour. Only recipes that need one read it; Grain does not.
    u32 argbSecondary = 0u;

    /// Grain only: how far the noise pushes each channel, in 0-255 units.
    f32 roughness = 0.0f;
    /// Grain only: decorrelates layers that would otherwise share a pattern.
    u32 seed = 0u;
};

/// Texture array layers, in upload order. The index of an entry *is* its layer in
/// the `GL_TEXTURE_2D_ARRAY`, and is what a Quad's material field holds.
inline constexpr std::array kLayers{
    LayerInfo{"stone",      TextureRecipe::Grain,     0xFF8C8C8Cu, 0u,          20.0f, 1u},
    LayerInfo{"dirt",       TextureRecipe::Grain,     0xFF6B4A2Fu, 0u,          18.0f, 2u},
    LayerInfo{"grass_top",  TextureRecipe::Grain,     0xFF5FA341u, 0u,          16.0f, 3u},
    LayerInfo{"grass_side", TextureRecipe::GrassSide, 0xFF6B4A2Fu, 0xFF5FA341u,  0.0f, 0u},
    LayerInfo{"sand",       TextureRecipe::Grain,     0xFFD8CA8Cu, 0u,          10.0f, 5u},

    // Deep stone. Deepslate is deliberately darker and less rough than stone: the
    // point of the Y 8 to 0 transition is that you can see you have crossed it.
    LayerInfo{"bedrock",    TextureRecipe::Grain,     0xFF565658u, 0u,          38.0f, 6u},
    LayerInfo{"deepslate",  TextureRecipe::Grain,     0xFF4F4F55u, 0u,          13.0f, 7u},

    // Stone variants and gravel. Their whole job is to break up an otherwise
    // uniform grey, so they are separated by value as much as by hue -- diorite
    // reads light, tuff dark, andesite between them.
    LayerInfo{"granite",    TextureRecipe::Grain,     0xFF9A6B5Bu, 0u,          17.0f, 8u},
    LayerInfo{"diorite",    TextureRecipe::Grain,     0xFFCDCDD0u, 0u,          24.0f, 9u},
    LayerInfo{"andesite",   TextureRecipe::Grain,     0xFF7E8280u, 0u,          14.0f, 10u},
    LayerInfo{"tuff",       TextureRecipe::Grain,     0xFF6C6E5Fu, 0u,          19.0f, 11u},
    LayerInfo{"gravel",     TextureRecipe::Grain,     0xFF8A8687u, 0u,          30.0f, 12u},

    // Ores, twice over: the same speckle colour on stone and on deepslate. The
    // host colours here must stay equal to the "stone" and "deepslate" entries
    // above, or an ore block would not sit in the rock it replaced.
    LayerInfo{"coal_ore",             TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFF191919u, 20.0f, 20u},
    LayerInfo{"deepslate_coal_ore",   TextureRecipe::Ore, 0xFF4F4F55u, 0xFF191919u, 13.0f, 21u},
    LayerInfo{"iron_ore",             TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFFD8AF93u, 20.0f, 22u},
    LayerInfo{"deepslate_iron_ore",   TextureRecipe::Ore, 0xFF4F4F55u, 0xFFD8AF93u, 13.0f, 23u},
    LayerInfo{"copper_ore",           TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFFD8794Au, 20.0f, 24u},
    LayerInfo{"deepslate_copper_ore", TextureRecipe::Ore, 0xFF4F4F55u, 0xFFD8794Au, 13.0f, 25u},
    LayerInfo{"gold_ore",             TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFFFCEE4Bu, 20.0f, 26u},
    LayerInfo{"deepslate_gold_ore",   TextureRecipe::Ore, 0xFF4F4F55u, 0xFFFCEE4Bu, 13.0f, 27u},
    LayerInfo{"redstone_ore",         TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFFAA0F01u, 20.0f, 28u},
    LayerInfo{"deepslate_redstone_ore", TextureRecipe::Ore, 0xFF4F4F55u, 0xFFAA0F01u, 13.0f, 29u},
    LayerInfo{"lapis_ore",            TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFF1B57B5u, 20.0f, 30u},
    LayerInfo{"deepslate_lapis_ore",  TextureRecipe::Ore, 0xFF4F4F55u, 0xFF1B57B5u, 13.0f, 31u},
    LayerInfo{"diamond_ore",          TextureRecipe::Ore, 0xFF8C8C8Cu, 0xFF5DECF5u, 20.0f, 32u},
    LayerInfo{"deepslate_diamond_ore", TextureRecipe::Ore, 0xFF4F4F55u, 0xFF5DECF5u, 13.0f, 33u},

    // Trees. The leaf colour is deliberately darker and bluer than grass_top: a
    // canopy that matches the ground it sits on reads as a flat green smear from any
    // distance, and separating them by value is what gives a tree a silhouette.
    LayerInfo{"oak_log_top",  TextureRecipe::Rings, 0xFF9C7A4Bu, 0xFF6B5230u, 6.0f, 40u},
    LayerInfo{"oak_log_side", TextureRecipe::Bark,  0xFF6B5433u, 0xFF4E3D24u, 14.0f, 41u},
    LayerInfo{"oak_leaves",   TextureRecipe::Grain, 0xFF3F7A2Eu, 0u,          22.0f, 42u},

    // Cobblestone exists because stone drops it. Rougher and a shade darker than
    // stone, which is the whole visual difference in vanilla too.
    LayerInfo{"cobblestone",  TextureRecipe::Grain, 0xFF7F7F7Fu, 0u,          34.0f, 43u},
};

inline constexpr u16 kTextureLayerCount = static_cast<u16>(kLayers.size());

/// Resolves a layer name to its array index, during constant evaluation.
///
/// `consteval` rather than `constexpr`: this must never be reachable at runtime,
/// because the whole benefit is that an unknown name cannot survive to become a
/// wrong texture. An unmatched name falls off the end into a throw, which is not a
/// constant expression, so the compiler rejects the call site.
consteval u16 layerOf(std::string_view name) {
    for (usize i = 0; i < kLayers.size(); ++i) {
        if (kLayers[i].name == name) {
            return static_cast<u16>(i);
        }
    }
    throw "unknown texture layer name";
}

/// Per-block-type properties. The mesher only ever asks for `opaque` and a face's
/// layer; everything else here exists for logging and for the texture generator.
struct BlockInfo {
    std::string_view name;

    /// Fully hides the neighbouring face. Air and (later) glass and water are not.
    bool opaque = true;

    /// Which layer each face draws. Three fields rather than six because no block
    /// so far distinguishes its four sides, and a block that does can be given a
    /// wider rule here without touching the mesher.
    u16 top = 0;
    u16 side = 0;
    u16 bottom = 0;

    /// One character, for the cross-sections `--probe` prints.
    ///
    /// Data rather than a switch somewhere in the probe, for the same reason as
    /// everything else here: a block type added to this table should not need a
    /// second edit elsewhere to become visible in the tool used to check it.
    char glyph = '?';

    /// Rock that a blob feature is allowed to replace: stone, deepslate, the four
    /// stone variants. Vanilla's ore features carry this as an explicit `targets`
    /// list per ore; every overworld ore names the same six blocks, so one flag on
    /// the target says the same thing without repeating it eight times.
    ///
    /// Deliberately false for gravel, dirt and the surface blocks. An ore that
    /// replaced the beach would be visible from the sky, and one that replaced
    /// gravel would let a blob eat a feature placed before it.
    bool stoneLike = false;

    /// Vanilla hardness. Sourced from the game's own block report -- see
    /// RESEARCH.md 8, which also records the drop and tool data that is *not* here.
    ///
    /// **Negative means unbreakable**, which is bedrock and only bedrock. Vanilla
    /// spells it -1 for the same reason: there is no hardness large enough to mean
    /// "never", so it is a separate case wearing a number.
    ///
    /// Break time comes out of this in `breakSeconds` below. Nothing else reads it.
    f32 hardness = 0.0f;

    /// What breaking this yields, by **name**. Empty means it drops itself, which is
    /// almost every block; `"air"` means it drops nothing.
    ///
    /// A name rather than a `BlockId` for a mechanical reason: `blockIdOf` cannot be
    /// called inside the very table it searches. `dropOf` resolves this at the use
    /// site, and the static_assert at the bottom of the file proves every name here
    /// matches something -- so a typo is still a compile error, which is the
    /// property this whole file exists to keep.
    std::string_view drops = {};
};

/// A block that draws the same layer on all six faces, which is most of them.
consteval BlockInfo uniformBlock(std::string_view name, std::string_view layer,
                                 char glyph, f32 hardness, bool stoneLike = false,
                                 std::string_view drops = {}) {
    const u16 index = layerOf(layer);
    return BlockInfo{name, true, index, index, index, glyph, stoneLike, hardness, drops};
}

/// Block types, in BlockId order. The index of an entry *is* its BlockId.
///
/// Air must stay first: `kAirBlock` is 0 in core/Types.hpp, where the mesher and
/// the palette both need it without knowing this table exists. The static_assert
/// below is what keeps the two from drifting.
/// Hardness values are vanilla's, from the game's own block report. RESEARCH.md 8
/// carries the table and the sources; `breakSeconds` below turns them into time.
inline constexpr std::array kBlocks{
    BlockInfo{"air", false, layerOf("stone"), layerOf("stone"), layerOf("stone"), '.'},

    // Stone drops cobblestone, and grass drops dirt. Both are vanilla, and both are
    // the reason `drops` exists rather than every block simply yielding itself.
    uniformBlock("stone", "stone", '#', 1.5f, true, "cobblestone"),
    uniformBlock("dirt", "dirt", 'd', 0.5f),
    // Grass is the reason a face carries a layer rather than a block id: one block
    // type, three different textures.
    BlockInfo{"grass", true, layerOf("grass_top"), layerOf("grass_side"),
                             layerOf("dirt"), 'g', false, 0.6f, "dirt"},
    uniformBlock("sand", "sand", 's', 0.5f),
    uniformBlock("cobblestone", "cobblestone", 'c', 2.0f, true),

    // -1 is vanilla's spelling of "never". See BlockInfo::hardness.
    uniformBlock("bedrock", "bedrock", 'B', -1.0f),
    uniformBlock("deepslate", "deepslate", 'D', 3.0f, true),

    uniformBlock("granite", "granite", 'r', 1.5f, true),
    uniformBlock("diorite", "diorite", 'o', 1.5f, true),
    uniformBlock("andesite", "andesite", 'a', 1.5f, true),
    uniformBlock("tuff", "tuff", 't', 1.5f, true),
    uniformBlock("gravel", "gravel", 'v', 0.6f),

    // Ores. Each is two block types, because replacing deepslate has to yield the
    // deepslate variant -- the ore keeps its speckle colour and takes the host
    // rock's. Uppercase glyphs so a cross-section shows ore against rock at a
    // glance; the deepslate variant shares its letter with the stone one, since
    // which rock it sits in is already obvious from the depth.
    // Every overworld ore is hardness 3, and every deepslate variant is 4.5 -- the
    // deep rock is what is hard, not the metal in it.
    uniformBlock("coal_ore", "coal_ore", 'C', 3.0f),
    uniformBlock("deepslate_coal_ore", "deepslate_coal_ore", 'C', 4.5f),
    uniformBlock("iron_ore", "iron_ore", 'I', 3.0f),
    uniformBlock("deepslate_iron_ore", "deepslate_iron_ore", 'I', 4.5f),
    uniformBlock("copper_ore", "copper_ore", 'U', 3.0f),
    uniformBlock("deepslate_copper_ore", "deepslate_copper_ore", 'U', 4.5f),
    uniformBlock("gold_ore", "gold_ore", 'G', 3.0f),
    uniformBlock("deepslate_gold_ore", "deepslate_gold_ore", 'G', 4.5f),
    uniformBlock("redstone_ore", "redstone_ore", 'R', 3.0f),
    uniformBlock("deepslate_redstone_ore", "deepslate_redstone_ore", 'R', 4.5f),
    uniformBlock("lapis_ore", "lapis_ore", 'L', 3.0f),
    uniformBlock("deepslate_lapis_ore", "deepslate_lapis_ore", 'L', 4.5f),
    uniformBlock("diamond_ore", "diamond_ore", 'X', 3.0f),
    uniformBlock("deepslate_diamond_ore", "deepslate_diamond_ore", 'X', 4.5f),

    // -- trees ----------------------------------------------------------------
    // Leaves are an opaque cube here, which is what makes trees cost nothing to
    // render. Vanilla's leaves are a full cube too and merely let light through;
    // "fast graphics" draws them exactly like this. Cross-quad geometry is what
    // grass and flowers need, and that is Phase 10 -- see RESEARCH.md 5.4.
    BlockInfo{"oak_log", true, layerOf("oak_log_top"), layerOf("oak_log_side"),
                               layerOf("oak_log_top"), 'T', false, 2.0f},
    // Leaves drop nothing. Vanilla drops the occasional sapling or apple, and
    // neither exists here -- a sapling is a plant, which is Phase 10's geometry.
    uniformBlock("oak_leaves", "oak_leaves", 'l', 0.2f, false, "air"),
};

/// Resolves a block name to its BlockId. Same reasoning as `layerOf`.
consteval BlockId blockIdOf(std::string_view name) {
    for (usize i = 0; i < kBlocks.size(); ++i) {
        if (kBlocks[i].name == name) {
            return static_cast<BlockId>(i);
        }
    }
    throw "unknown block name";
}

// Block ids. Declared here rather than beside the registry, because the value of
// each one is a property of the table above and of nothing else.
inline constexpr BlockId kStoneBlock     = blockIdOf("stone");
inline constexpr BlockId kDirtBlock      = blockIdOf("dirt");
inline constexpr BlockId kGrassBlock     = blockIdOf("grass");
inline constexpr BlockId kSandBlock      = blockIdOf("sand");
inline constexpr BlockId kBedrockBlock   = blockIdOf("bedrock");
inline constexpr BlockId kDeepslateBlock = blockIdOf("deepslate");
inline constexpr BlockId kOakLogBlock    = blockIdOf("oak_log");
inline constexpr BlockId kOakLeavesBlock = blockIdOf("oak_leaves");

/// True for the rock a blob feature may replace. See BlockInfo::stoneLike.
constexpr bool isStoneLike(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].stoneLike;
}

/// What breaking `id` yields. `kAirBlock` means nothing drops.
///
/// A linear search over the table, and that is fine: this runs once per block
/// broken, which is once per click, against 29 entries.
constexpr BlockId dropOf(BlockId id) {
    if (id >= kBlocks.size()) {
        return kAirBlock;
    }
    const std::string_view name = kBlocks[id].drops;
    if (name.empty()) {
        return id; // Drops itself, which is almost everything.
    }
    for (usize i = 0; i < kBlocks.size(); ++i) {
        if (kBlocks[i].name == name) {
            return static_cast<BlockId>(i);
        }
    }
    return kAirBlock;
}

/// Bedrock, and anything else ever given a negative hardness.
constexpr bool isUnbreakable(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].hardness < 0.0f;
}

/// How long `id` takes to break bare-handed, in seconds. Zero for air, and for
/// anything unbreakable -- callers are expected to ask `isUnbreakable` first.
///
/// Vanilla computes damage per tick as `speed / hardness / (canHarvest ? 30 : 100)`
/// and breaks the block when that reaches 1. At bare-hand speed 1.0 that is
/// `hardness * 1.5` seconds when the block can be harvested and `hardness * 5` when
/// it cannot -- the second branch being the game's way of saying "you need a
/// pickaxe", since it also drops nothing.
///
/// **This engine takes the harvestable branch for everything, deliberately.** With
/// no tools the other branch is not "harder", it is a dead end: bare-handed stone in
/// vanilla is 7.5 seconds for nothing at all. Tools arrive later as the `speed`
/// multiplier this formula already has room for, and none of the hardness numbers in
/// the table change when they do.
constexpr f32 breakSeconds(BlockId id) {
    if (id >= kBlocks.size() || isUnbreakable(id)) {
        return 0.0f;
    }
    return kBlocks[id].hardness * 1.5f;
}

/// Proves every `drops` name in the table matches a real block.
///
/// `dropOf` has to be a runtime-callable `constexpr` and so cannot reject a bad name
/// the way `blockIdOf` does -- it returns air instead, which would silently mean "no
/// drop". This puts the compile error back.
consteval bool everyDropResolves() {
    for (usize i = 0; i < kBlocks.size(); ++i) {
        const std::string_view name = kBlocks[i].drops;
        if (name.empty()) {
            continue;
        }
        bool found = false;
        for (const BlockInfo& candidate : kBlocks) {
            found = found || candidate.name == name;
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static_assert(everyDropResolves(),
              "a block's drops name does not match any block in kBlocks");

static_assert(blockIdOf("air") == kAirBlock,
              "air must be the first entry in kBlocks; core/Types.hpp fixes it at 0");
static_assert(kBlocks.size() <= 256,
              "past 256 block types the palette's 8-bit index width no longer covers "
              "a section that holds one of everything");

} // namespace mc
