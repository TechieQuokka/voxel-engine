#pragma once

#include "core/Types.hpp"
#include "world/Tools.hpp"

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
    /// Crossed ripples, for a water surface. `Grain` at a roughness low enough not to
    /// look like gravel produces no visible texture at all, and a surface with no
    /// texture does not read as a surface -- see the note at `generateWaterSurface`.
    Water,
    /// Concentric growth rings in a second colour, for the cut end of a log. The
    /// one thing that makes a log read as a log rather than as a brown cube.
    Rings,
    /// Vertical streaks rather than isotropic noise, for bark. Grain would give a
    /// log the same texture as dirt, and the grain direction is most of what says
    /// which way the trunk runs.
    Bark,

    /// Horizontal boards with seams between them, for planks. Bark rotated would be
    /// the cheap version and would read as a log lying down; a plank face is defined
    /// by the *seams*, which are darker lines at fixed intervals rather than noise.
    Planks,

    /// Planks with a 3x3 lattice cut into them, for the top of a crafting table.
    ///
    /// **The pattern is the label.** A player who cannot find the table cannot craft
    /// at all, and that is not a hypothetical -- the first person to play Phase 16
    /// could not find the crafting grid because it was inside a window rather than
    /// on a block. A cube of plain planks would repeat the mistake in a new place, so
    /// the block says what it is from the one angle you look at it from.
    CraftGrid,

    /// Grain with a dark arched mouth low on the tile: the side of a furnace.
    ///
    /// Same argument as `CraftGrid`. A furnace that reads as a grey cube is a furnace
    /// the player walks past, and this phase exists because the last unrecognisable
    /// block cost a whole play session.
    Furnace,

    /// A shaft with a flame on top, drawn up the middle of a dark tile.
    ///
    /// **The dark tile is the temporary part; the art on it is not.** A torch is not
    /// a cube, and until Phase 10 gives the block real geometry this one is drawn on
    /// six cube faces -- so the tile has to be filled rather than transparent, or the
    /// cube would have holes in it. The shaft and flame sit where vanilla puts them,
    /// so when the geometry lands the only change is that the backdrop is discarded
    /// on alpha, exactly as the item icons below already are. See DESIGN.md 7.26.
    Torch,

    // -- item icons ------------------------------------------------------------
    //
    // **These layers are never meshed.** They live in the same texture array only
    // because the HUD already binds it to draw slot contents, and a second array for
    // fourteen 16x16 icons would be a second binding, a second upload and a second
    // thing to keep in step. Nothing sets a Quad's `material` to one of them.
    //
    // They are the first layers with a **transparent background**: an icon is a shape
    // on nothing, where every block texture fills its tile. `hud.frag` and `item.frag`
    // both discard on alpha for this, which they did not need to before.

    /// A rounded lump in the middle of the tile. Coal, diamond, redstone, lapis --
    /// everything that comes out of rock as a thing rather than as a block.
    Nugget,
    /// A short diagonal shaft. The one item that is not made of anything else.
    Stick,
    /// A flat bar. Everything that comes out of a furnace rather than out of rock,
    /// and shaped unlike `Nugget` on purpose -- "smelted" has to read at slot size.
    Ingot,
    /// The four tools. Separate recipes rather than one with a shape parameter,
    /// because the switch in BlockTextures.cpp is deliberately unguarded by a
    /// default -- a fifth tool then fails to compile rather than drawing a blank.
    ToolPickaxe,
    ToolAxe,
    ToolShovel,
    ToolSword,
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

    // **The first layer whose alpha is not 255.** Vanilla's still-water colour, at
    // three-quarters opacity. The alpha channel has always been carried through
    // `fromArgb` -> `shade` -> `writePixel` and has always been opaque until now;
    // the texture array is SRGB8_ALPHA8, which encodes only RGB, so this value
    // reaches the shader unchanged.
    //
    // **`roughness` is a ripple amplitude here, not a grain amplitude**, and the
    // number went from 4 to 15 with the recipe change. Four units of isotropic noise
    // is invisible; fifteen units of crossed wave is a surface. The two are not the
    // same quantity even though they share the field -- see `generateWaterSurface`.
    LayerInfo{"water",        TextureRecipe::Water, 0xBF3F76E4u, 0u,          26.0f, 50u},

    // Planks. Lighter than the bark they come from, because a sawn face is the inside
    // of the trunk; the secondary colour is the seam between boards.
    LayerInfo{"oak_planks",   TextureRecipe::Planks, 0xFFB08A55u, 0xFF8A6A3Eu, 9.0f, 60u},

    // The crafting table: a lattice on the top, darker worked boards on the sides.
    // The bottom reuses `oak_planks`, which is what it is made of and what vanilla
    // shows there too.
    LayerInfo{"crafting_table_top",  TextureRecipe::CraftGrid, 0xFFA97F4Eu, 0xFF54402Bu, 8.0f, 61u},
    LayerInfo{"crafting_table_side", TextureRecipe::Planks,    0xFF8F6F45u, 0xFF5E4830u, 9.0f, 62u},

    // The furnace: rough stone with a darker mouth on its sides. One block rather
    // than vanilla's lit/unlit pair -- the mesher has no per-face orientation, so a
    // furnace already faces every direction at once, and a second block type for the
    // lit state would be a lie in four of them.
    LayerInfo{"furnace_top",  TextureRecipe::Grain,   0xFF7A7A7Au, 0u,          22.0f, 63u},
    LayerInfo{"furnace_side", TextureRecipe::Furnace, 0xFF7A7A7Au, 0xFF2B2B2Du, 22.0f, 64u},

    // The flame is the primary colour and the shaft the secondary, which is the same
    // order every two-colour recipe here uses: the thing that identifies the block
    // first. Vanilla's torch flame is a warm near-white; this is that, kept bright
    // enough to read against the backdrop at cube scale.
    LayerInfo{"torch",        TextureRecipe::Torch,   0xFFFFD98Au, 0xFF6B5230u,  9.0f, 65u},

    // -- item icons, transparent outside the shape ------------------------------
    // `argb` is the body and `argbSecondary` the shading or the handle. Roughness is
    // reused as the amount of per-pixel variation, which keeps an icon from reading
    // as flat vector art beside sixteen noisy block textures.
    LayerInfo{"stick",           TextureRecipe::Stick,  0xFF9C7A4Bu, 0xFF6B5230u, 3.0f, 70u},

    // Smelted metal. Phase 17's output, and the reason half the ore table stops
    // being decoration.
    LayerInfo{"iron_ingot",      TextureRecipe::Ingot,  0xFFD8D8D8u, 0u,          7.0f, 71u},
    LayerInfo{"copper_ingot",    TextureRecipe::Ingot,  0xFFD8794Au, 0u,          7.0f, 72u},
    LayerInfo{"gold_ingot",      TextureRecipe::Ingot,  0xFFFCEE4Bu, 0u,          7.0f, 73u},
    LayerInfo{"coal",            TextureRecipe::Nugget, 0xFF262626u, 0xFF0D0D0Du, 14.0f, 71u},
    LayerInfo{"diamond",         TextureRecipe::Nugget, 0xFF5DECF5u, 0xFF2FA8B4u, 12.0f, 72u},
    LayerInfo{"redstone",        TextureRecipe::Nugget, 0xFFAA0F01u, 0xFF6E0A00u, 12.0f, 73u},
    LayerInfo{"lapis_lazuli",    TextureRecipe::Nugget, 0xFF1B57B5u, 0xFF103675u, 12.0f, 74u},

    // Tools. The head colour is the tier and the handle is always oak, which is what
    // makes a row of them read as the same tool in four materials at a glance.
    //
    // **Their roughness is a third of a block's, and that is not a style choice.**
    // A held tool is the sprite extruded into a model (DESIGN.md 7.22), and the rim
    // faces take the colour of the pixel they came from -- so per-pixel noise on the
    // sprite becomes a barcode along every edge of the thing in your hand. Vanilla's
    // tool sprites are two or three flat shades for the same reason a pixel artist
    // would give: at sixteen pixels, noise reads as dirt rather than as material.
    LayerInfo{"wooden_pickaxe",  TextureRecipe::ToolPickaxe, 0xFFB08A55u, 0xFF9C7A4Bu, 3.0f, 80u},
    LayerInfo{"wooden_axe",      TextureRecipe::ToolAxe,     0xFFB08A55u, 0xFF9C7A4Bu, 3.0f, 81u},
    LayerInfo{"wooden_shovel",   TextureRecipe::ToolShovel,  0xFFB08A55u, 0xFF9C7A4Bu, 3.0f, 82u},
    LayerInfo{"wooden_sword",    TextureRecipe::ToolSword,   0xFFB08A55u, 0xFF9C7A4Bu, 3.0f, 83u},
    LayerInfo{"stone_pickaxe",   TextureRecipe::ToolPickaxe, 0xFF8C8C8Cu, 0xFF9C7A4Bu, 4.0f, 84u},
    LayerInfo{"stone_axe",       TextureRecipe::ToolAxe,     0xFF8C8C8Cu, 0xFF9C7A4Bu, 4.0f, 85u},
    LayerInfo{"stone_shovel",    TextureRecipe::ToolShovel,  0xFF8C8C8Cu, 0xFF9C7A4Bu, 4.0f, 86u},
    LayerInfo{"stone_sword",     TextureRecipe::ToolSword,   0xFF8C8C8Cu, 0xFF9C7A4Bu, 4.0f, 87u},

    // Iron and diamond. The two tiers smelting opens: iron needs an ingot, and
    // diamond needs an iron pickaxe to have been mined with.
    LayerInfo{"iron_pickaxe",    TextureRecipe::ToolPickaxe, 0xFFD8D8D8u, 0xFF9C7A4Bu, 4.0f, 88u},
    LayerInfo{"iron_axe",        TextureRecipe::ToolAxe,     0xFFD8D8D8u, 0xFF9C7A4Bu, 4.0f, 89u},
    LayerInfo{"iron_shovel",     TextureRecipe::ToolShovel,  0xFFD8D8D8u, 0xFF9C7A4Bu, 4.0f, 90u},
    LayerInfo{"iron_sword",      TextureRecipe::ToolSword,   0xFFD8D8D8u, 0xFF9C7A4Bu, 4.0f, 91u},
    LayerInfo{"diamond_pickaxe", TextureRecipe::ToolPickaxe, 0xFF4AEDD9u, 0xFF9C7A4Bu, 4.0f, 92u},
    LayerInfo{"diamond_axe",     TextureRecipe::ToolAxe,     0xFF4AEDD9u, 0xFF9C7A4Bu, 4.0f, 93u},
    LayerInfo{"diamond_shovel",  TextureRecipe::ToolShovel,  0xFF4AEDD9u, 0xFF9C7A4Bu, 4.0f, 94u},
    LayerInfo{"diamond_sword",   TextureRecipe::ToolSword,   0xFF4AEDD9u, 0xFF9C7A4Bu, 4.0f, 95u},
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

    /// Falls when whatever it was standing on goes away. Sand and gravel, which is
    /// exactly vanilla's list among the blocks this engine has.
    ///
    /// Read only by `BlockUpdates`, the block-update tick. A flag here rather than a
    /// `switch` on BlockId inside that tick, for the reason the whole file exists --
    /// see the note at the top and the one in HANDOFF.md 5.
    ///
    /// **Last field on purpose.** Every existing `BlockInfo{...}` in the table below
    /// is positional, so a new field anywhere else would silently shift the meaning
    /// of the entries that spell themselves out rather than going through
    /// `uniformBlock`. Appending cannot.
    bool falls = false;

    /// A liquid. Distinct from `!opaque` in every way that matters:
    ///
    /// - You walk **into** it, so it holds nothing up -- not the player, not a
    ///   dropped item, and not a block of sand looking for support.
    /// - The aim ray goes **through** it, so a crosshair over an ocean targets the
    ///   sea bed rather than the surface.
    /// - It is meshed in a **second pass** and drawn translucent, against a cull
    ///   mask that includes itself, so a body of water is a surface rather than a
    ///   stack of visible cubes.
    ///
    /// `opaque` answers "does this hide the face behind it", which is a rendering
    /// question. This answers "is this stuff", which is a physics one. Glass will
    /// want the first and not the second.
    bool fluid = false;

    /// Which tool mines this faster. `None` means no tool helps, which is true of
    /// leaves and of anything a hand is already as good on.
    ///
    /// **Speed and harvest are two different questions and this only answers the
    /// first.** An axe makes a log quicker; a log drops itself either way.
    ToolKind tool = ToolKind::None;

    /// The tool tier required to get a **drop** at all. `None` means bare hands are
    /// enough, which is most blocks.
    ///
    /// Vanilla's rule, and it is the whole reason a pickaxe is worth making: mining
    /// stone with a fist takes five times hardness instead of one and a half, and
    /// yields nothing when it finally breaks. Speed alone would make a tool an
    /// optimisation; this makes it a prerequisite.
    ///
    /// A non-None value here requires `tool` to be set too -- a tier with no kind
    /// would be unsatisfiable, since nothing can match a `ToolKind::None` requirement.
    /// The static_assert at the bottom of the file enforces it.
    ToolTier minTier = ToolTier::None;

    /// How empty this fluid is: 0 is a source block, 1-7 are flowing, one further
    /// from the source each step. Meaningless unless `fluid`.
    ///
    /// **Vanilla's fluid level, spelled as separate block types.** Minecraft carries
    /// it as block state on one block; this engine has no block state, and a section
    /// stores one `BlockId` per voxel through a palette already built to make
    /// near-uniform data free. Eight ids for water widens the palette only in the
    /// sections where water is actually flowing -- an ocean is uniform level 0 and
    /// stays a single palette entry, which is the whole reason this was affordable
    /// without inventing block state first.
    ///
    /// **Second to last field, and the rule that puts it here has now caught two
    /// bugs.** Every `BlockInfo{...}` in the table below is positional, so a field
    /// inserted rather than appended silently shifts the meaning of the ones after
    /// it -- the first draft of this put it before `minTier` and turned every ore's
    /// harvest tier into a fluid level. See the note on `falls` above.
    u8 fluidLevel = 0;

    /// A fluid that replenishes itself: an ocean block, or one placed by a bucket.
    ///
    /// **Distinct from `fluidLevel == 0`, and that distinction is vanilla's.** Water
    /// falling down a shaft is *also* full-strength -- it spreads seven blocks when it
    /// lands, because "the depth resets at a new elevation" -- but it is not a source:
    /// cut the supply above it and it drains, where a source stays forever. Vanilla
    /// spells the difference as levels 8-15 versus level 0; this engine spells it as
    /// one more block type and this flag.
    ///
    /// **A source is never consumed by flowing out of it.** That is the whole reason
    /// water is affordable: a conservative fluid would need per-body global state,
    /// which a chunk-streaming world cannot cheaply keep. RESEARCH.md 7.1.
    bool fluidSource = false;

    /// How much block light this emits, 0 to 15. Vanilla's own numbers: a torch is
    /// 14, and every block in this table that is not a torch is 0.
    ///
    /// **Zero for everything generated, and that is what makes the channel free.**
    /// No ore, no stone and no tree emits, so an untouched world's block light is a
    /// uniform zero `LightArray` in every section, holding no storage. The channel
    /// only starts costing memory where a player has put a torch.
    ///
    /// Read by `BlockLight`, which spreads it, and by nothing else.
    ///
    /// **Last field, and the rule that puts it here has now caught two bugs** -- see
    /// the notes on `falls` and `fluidLevel`. Every `BlockInfo{...}` below is
    /// positional, so a field inserted rather than appended silently reinterprets
    /// every entry after it.
    u8 luminance = 0;
};

/// A block that draws the same layer on all six faces, which is most of them.
consteval BlockInfo uniformBlock(std::string_view name, std::string_view layer,
                                 char glyph, f32 hardness, bool stoneLike = false,
                                 std::string_view drops = {}, bool falls = false,
                                 ToolKind tool = ToolKind::None,
                                 ToolTier minTier = ToolTier::None) {
    const u16 index = layerOf(layer);
    return BlockInfo{name, true, index, index, index, glyph, stoneLike, hardness,
                     drops, falls, false, tool, minTier};
}

/// A block mined with a pickaxe, which is every kind of rock.
///
/// Two of these three arguments are the same for all twenty of them, and writing
/// `false, {}, false, ToolKind::Pickaxe` twenty times would bury the one that varies
/// -- the tier, which is the difference between an ore you can reach and one you
/// cannot. `stoneLike` is true for every caller but the ores, which is the one
/// exception this spells out.
consteval BlockInfo rockBlock(std::string_view name, std::string_view layer, char glyph,
                              f32 hardness, ToolTier minTier, bool stoneLike = false,
                              std::string_view drops = {}) {
    return uniformBlock(name, layer, glyph, hardness, stoneLike, drops, false,
                        ToolKind::Pickaxe, minTier);
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
    //
    // **Stone is the block that makes a pickaxe worth making.** A fist takes 7.5
    // seconds on it and yields nothing at the end, because `minTier` is Wood and a
    // fist is None; a wooden pickaxe takes 1.1 seconds and yields cobblestone.
    rockBlock("stone", "stone", '#', 1.5f, ToolTier::Wood, true, "cobblestone"),
    uniformBlock("dirt", "dirt", 'd', 0.5f, false, {}, false, ToolKind::Shovel),
    // Grass is the reason a face carries a layer rather than a block id: one block
    // type, three different textures.
    BlockInfo{"grass", true, layerOf("grass_top"), layerOf("grass_side"),
                             layerOf("dirt"), 'g', false, 0.6f, "dirt", false, false,
                             ToolKind::Shovel},
    // Sand and gravel fall. The `true` after the empty drops is `BlockInfo::falls`,
    // and it is the whole of what makes a sand ceiling collapse when dug out from
    // under.
    uniformBlock("sand", "sand", 's', 0.5f, false, {}, true, ToolKind::Shovel),
    rockBlock("cobblestone", "cobblestone", 'c', 2.0f, ToolTier::Wood, true),

    // -1 is vanilla's spelling of "never". See BlockInfo::hardness. No tier, because
    // no tier would ever be enough -- unbreakable is checked before harvest is.
    uniformBlock("bedrock", "bedrock", 'B', -1.0f),
    rockBlock("deepslate", "deepslate", 'D', 3.0f, ToolTier::Wood, true),

    rockBlock("granite", "granite", 'r', 1.5f, ToolTier::Wood, true),
    rockBlock("diorite", "diorite", 'o', 1.5f, ToolTier::Wood, true),
    rockBlock("andesite", "andesite", 'a', 1.5f, ToolTier::Wood, true),
    rockBlock("tuff", "tuff", 't', 1.5f, ToolTier::Wood, true),
    uniformBlock("gravel", "gravel", 'v', 0.6f, false, {}, true, ToolKind::Shovel),

    // Ores. Each is two block types, because replacing deepslate has to yield the
    // deepslate variant -- the ore keeps its speckle colour and takes the host
    // rock's. Uppercase glyphs so a cross-section shows ore against rock at a
    // glance; the deepslate variant shares its letter with the stone one, since
    // which rock it sits in is already obvious from the depth.
    // Every overworld ore is hardness 3, and every deepslate variant is 4.5 -- the
    // deep rock is what is hard, not the metal in it.
    // **The tier column is vanilla's, and it is why half of these are out of reach in
    // Phase 16.** Coal needs wood; iron, copper and lapis need stone; gold, redstone
    // and diamond need iron -- and an iron pickaxe needs an ingot, an ingot needs
    // smelting, and a furnace is Phase 17. Mining a diamond with a stone pickaxe
    // breaks the block and yields nothing, which is exactly what vanilla does and is
    // the engine saying "not yet" in the only way a player will believe.
    //
    // **The ores that drop an item rather than themselves are the split working.**
    // `dropOf` resolves these names across blocks *and* items, so "coal" here is an
    // ItemId that no `BlockId` can express -- which is the entire point of Phase 16.
    rockBlock("coal_ore", "coal_ore", 'C', 3.0f, ToolTier::Wood, false, "coal"),
    rockBlock("deepslate_coal_ore", "deepslate_coal_ore", 'C', 4.5f, ToolTier::Wood, false, "coal"),
    rockBlock("iron_ore", "iron_ore", 'I', 3.0f, ToolTier::Stone),
    rockBlock("deepslate_iron_ore", "deepslate_iron_ore", 'I', 4.5f, ToolTier::Stone),
    rockBlock("copper_ore", "copper_ore", 'U', 3.0f, ToolTier::Stone),
    rockBlock("deepslate_copper_ore", "deepslate_copper_ore", 'U', 4.5f, ToolTier::Stone),
    rockBlock("gold_ore", "gold_ore", 'G', 3.0f, ToolTier::Iron),
    rockBlock("deepslate_gold_ore", "deepslate_gold_ore", 'G', 4.5f, ToolTier::Iron),
    rockBlock("redstone_ore", "redstone_ore", 'R', 3.0f, ToolTier::Iron, false, "redstone"),
    rockBlock("deepslate_redstone_ore", "deepslate_redstone_ore", 'R', 4.5f, ToolTier::Iron, false, "redstone"),
    rockBlock("lapis_ore", "lapis_ore", 'L', 3.0f, ToolTier::Stone, false, "lapis_lazuli"),
    rockBlock("deepslate_lapis_ore", "deepslate_lapis_ore", 'L', 4.5f, ToolTier::Stone, false, "lapis_lazuli"),
    rockBlock("diamond_ore", "diamond_ore", 'X', 3.0f, ToolTier::Iron, false, "diamond"),
    rockBlock("deepslate_diamond_ore", "deepslate_diamond_ore", 'X', 4.5f, ToolTier::Iron, false, "diamond"),

    // -- trees ----------------------------------------------------------------
    // Leaves are an opaque cube here, which is what makes trees cost nothing to
    // render. Vanilla's leaves are a full cube too and merely let light through;
    // "fast graphics" draws them exactly like this. Cross-quad geometry is what
    // grass and flowers need, and that is Phase 10 -- see RESEARCH.md 5.4.
    BlockInfo{"oak_log", true, layerOf("oak_log_top"), layerOf("oak_log_side"),
                               layerOf("oak_log_top"), 'T', false, 2.0f, {}, false,
                               false, ToolKind::Axe},
    // Leaves drop nothing. Vanilla drops the occasional sapling or apple, and
    // neither exists here -- a sapling is a plant, which is Phase 10's geometry.
    uniformBlock("oak_leaves", "oak_leaves", 'l', 0.2f, false, "air"),

    // **Planks are the first block that is only ever crafted.** Nothing in the
    // generator places one and no ore drops one, so the only path to it is a log in
    // the grid -- which makes it the shortest proof that crafting is wired all the
    // way from a recipe to a placed block.
    uniformBlock("oak_planks", "oak_planks", 'p', 2.0f, false, {}, false, ToolKind::Axe),

    // **The first block you use rather than stand on.** Right-clicking it opens a 3x3
    // grid, which is the only way to reach a pickaxe -- the player's own grid is 2x2
    // and a pickaxe does not fit in four cells. Vanilla's gate, and the reason the
    // table exists at all rather than being a decoration.
    //
    // Vanilla hardness is 2.5. Bottom face is plain planks, which is what it is made
    // of; see the layer table for why the top is not.
    BlockInfo{"crafting_table", true, layerOf("crafting_table_top"),
                                      layerOf("crafting_table_side"),
                                      layerOf("oak_planks"), 'w', false, 2.5f, {},
                                      false, false, ToolKind::Axe},

    // **The block that opens the ore table.** Iron, copper and gold come out of here
    // and nowhere else, and half of `kBlocks` above is gated behind the pickaxes they
    // make. Vanilla hardness 3.5, and it needs a pickaxe to drop -- a furnace is
    // stone, and mining it bare-handed giving nothing is the same rule as stone.
    BlockInfo{"furnace", true, layerOf("furnace_top"), layerOf("furnace_side"),
                               layerOf("furnace_top"), 'F', false, 3.5f, {}, false,
                               false, ToolKind::Pickaxe, ToolTier::Wood},

    // **The first block that gives rather than takes.** Everything above this either
    // holds the player up or is carried; a torch changes what can be seen, and it is
    // the reason a room with a roof on it is somewhere you can be. The trailing 14 is
    // `luminance`, vanilla's own number for a torch.
    //
    // Hardness 0: vanilla breaks a torch instantly and with any tool, including none.
    //
    // **Opaque, and that is the deviation.** A vanilla torch is a thin cross that
    // hides nothing; this one is a full cube, because the mesher draws cubes and
    // nothing else -- a non-opaque block emits no faces at all and the light would
    // come from something invisible. So it blocks sky light and casts a shadow it has
    // no business casting, and that is the price of having light at all before the
    // geometry it wants. **Phase 10 flips this one field to `false` and gives it a
    // shape; nothing else about the block, the recipe or the light changes.** The
    // choice and its cost are written up in DESIGN.md 7.26.
    BlockInfo{"torch", true, layerOf("torch"), layerOf("torch"), layerOf("torch"),
                             'T', false, 0.0f, {}, false, false, ToolKind::None,
                             ToolTier::None, 0, false, 14},

    // -- water ----------------------------------------------------------------
    // Not opaque, so it hides nothing behind it; a fluid, so it holds nothing up.
    // Unbreakable because a bucket is the only thing that removes water in vanilla
    // and there are no items yet -- and because the aim ray passes through it, the
    // player never gets the chance to try.
    // The trailing `0, true` is `fluidLevel` then `fluidSource`.
    BlockInfo{"water", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '~', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None,
              0, true},

    // **Water on its way down.** Full strength like a source -- it spreads seven
    // blocks where it lands, because vanilla resets the depth at each new elevation --
    // but not a source: remove what feeds it and it drains. Vanilla carries this as
    // levels 8-15 on the same block; here it is one more block type, which is what
    // an engine with no block state has instead.
    BlockInfo{"water_falling", false, layerOf("water"), layerOf("water"),
              layerOf("water"), 'v', false, -1.0f, "air", false, true, ToolKind::None,
              ToolTier::None, 0, false},

    // **Flowing water, one block type per level.** Level 1 is next to a source and
    // level 7 is as far as water reaches on a flat floor; there is no level 8 because
    // that is where vanilla stops and the block becomes air.
    //
    // Seven entries rather than a `level` field on one block type, because a section
    // stores a `BlockId` per voxel and nothing else -- see BlockInfo::fluidLevel.
    // They are identical to the source in every way that rendering and physics care
    // about, which is why they share its texture and its `-1` hardness: a bucket is
    // the only thing that removes water in vanilla, the aim ray passes through a
    // fluid, and a player never gets the chance to try.
    //
    // The trailing `1`..`7` is `BlockInfo::fluidLevel`, the last field.
    BlockInfo{"water_1", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '1', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 1},
    BlockInfo{"water_2", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '2', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 2},
    BlockInfo{"water_3", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '3', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 3},
    BlockInfo{"water_4", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '4', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 4},
    BlockInfo{"water_5", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '5', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 5},
    BlockInfo{"water_6", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '6', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 6},
    BlockInfo{"water_7", false, layerOf("water"), layerOf("water"), layerOf("water"),
              '7', false, -1.0f, "air", false, true, ToolKind::None, ToolTier::None, 7},
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
inline constexpr BlockId kGravelBlock    = blockIdOf("gravel");
inline constexpr BlockId kBedrockBlock   = blockIdOf("bedrock");
inline constexpr BlockId kDeepslateBlock = blockIdOf("deepslate");
inline constexpr BlockId kOakLogBlock    = blockIdOf("oak_log");
inline constexpr BlockId kOakLeavesBlock = blockIdOf("oak_leaves");
inline constexpr BlockId kOakPlanksBlock = blockIdOf("oak_planks");
/// The one block so far that is *used* rather than built against. `Engine` compares
/// against this to decide whether a right click opens a window or places a block.
inline constexpr BlockId kCraftingTableBlock = blockIdOf("crafting_table");
inline constexpr BlockId kFurnaceBlock = blockIdOf("furnace");
inline constexpr BlockId kTorchBlock     = blockIdOf("torch");
inline constexpr BlockId kWaterBlock     = blockIdOf("water");

/// Sea level. Vanilla's 63 is the first block *above* the water surface, so the
/// topmost water block is 62 -- which is the number this engine needs, since it
/// fills downward from here.
inline constexpr i32 kSeaLevel = 62;

/// True for the rock a blob feature may replace. See BlockInfo::stoneLike.
constexpr bool isStoneLike(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].stoneLike;
}

/// True for the blocks that fall when unsupported. See BlockInfo::falls.
constexpr bool isFalling(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].falls;
}

/// True for a block that hides the face behind it. See BlockInfo::opaque.
///
/// **The out-of-range answer is `true` here and `false` in every other predicate
/// above**, because it follows the field's own default rather than the shape of the
/// expression: an unknown block hiding what is behind it is the harmless direction,
/// and it is the same answer `BlockLight` already gives for a column that has not
/// loaded. `BlockRegistry::isOpaque` is the runtime form of this; the mesher holds a
/// registry and uses that one.
constexpr bool isOpaque(BlockId id) {
    return id >= kBlocks.size() || kBlocks[id].opaque;
}

/// True for liquids. See BlockInfo::fluid.
constexpr bool isFluid(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].fluid;
}

/// How empty a fluid is: 0 for a source, 1-7 flowing. See BlockInfo::fluidLevel.
/// Zero for anything that is not a fluid, so ask `isFluid` first.
constexpr u8 fluidLevelOf(BlockId id) {
    return id < kBlocks.size() ? kBlocks[id].fluidLevel : u8{0};
}

/// The furthest a fluid reaches from its source on a flat floor. Past this the block
/// would be level 8, and vanilla makes that air instead.
inline constexpr u8 kMaxFluidLevel = 7;

/// A fluid that replenishes itself rather than draining. See BlockInfo::fluidSource.
constexpr bool isFluidSource(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].fluidSource;
}

/// How much block light this emits, 0 to 15. See BlockInfo::luminance.
constexpr u8 luminanceOf(BlockId id) {
    return id < kBlocks.size() ? kBlocks[id].luminance : u8{0};
}

/// True for a block that lights its surroundings.
constexpr bool isEmitter(BlockId id) { return luminanceOf(id) != 0; }

/// The brightest any block light can be, and the level a source cell holds.
inline constexpr u8 kMaxLight = 15;

/// The *flowing* water block at `level`, or `kAirBlock` past `kMaxFluidLevel`.
///
/// Level 0 gives the falling block, not the source: a flow can produce full-strength
/// water on its way down but must never produce a source, because a source never
/// drains and one created by accident is a leak that fills the world.
///
/// A search rather than arithmetic on `kWaterBlock + level`. The ids happen to be
/// consecutive today and relying on that would make inserting a block type between
/// them a silent behaviour change rather than a compile error -- which is the failure
/// mode this whole file exists to prevent.
constexpr BlockId waterAtLevel(u8 level) {
    if (level > kMaxFluidLevel) {
        return kAirBlock;
    }
    for (usize i = 0; i < kBlocks.size(); ++i) {
        if (kBlocks[i].fluid && !kBlocks[i].fluidSource && kBlocks[i].fluidLevel == level) {
            return static_cast<BlockId>(i);
        }
    }
    return kAirBlock;
}

/// True for water at any level, source or flowing.
constexpr bool isWater(BlockId id) { return isFluid(id); }

/// True for anything that holds things up: not air, and not a liquid.
///
/// **This is the test almost every caller of `blockAt` actually wanted**, and until
/// water existed `!= kAirBlock` was indistinguishable from it. Walking, item
/// physics, falling-block support and the aim ray all need this one rather than
/// that one; getting it wrong means a player standing on an ocean.
constexpr bool isSolidBlock(BlockId id) {
    return id != kAirBlock && !isFluid(id);
}

/// Can a fluid move into this block and replace it?
///
/// Air and other fluid, which today is the same thing as "not solid". Vanilla also
/// washes away torches and flowers; neither exists yet, and when they do this is the
/// one place that has to learn about them.
constexpr bool isFluidReplaceable(BlockId id) { return !isSolidBlock(id); }

/// Bedrock, and anything else ever given a negative hardness.
constexpr bool isUnbreakable(BlockId id) {
    return id < kBlocks.size() && kBlocks[id].hardness < 0.0f;
}

/// Which tool speeds this block up. See BlockInfo::tool.
constexpr ToolKind toolFor(BlockId id) {
    return id < kBlocks.size() ? kBlocks[id].tool : ToolKind::None;
}

/// The tool tier needed to get a drop. See BlockInfo::minTier.
constexpr ToolTier minTierOf(BlockId id) {
    return id < kBlocks.size() ? kBlocks[id].minTier : ToolTier::None;
}

/// **`dropOf` and `breakSeconds` used to live here and now live in `ItemTable.hpp`**,
/// which is not a tidying move. Both changed shape in Phase 16 and both changed in
/// the same direction: what a block drops is an `ItemId` rather than a `BlockId`
/// (coal ore drops coal, which is not a block), and how long it takes to break
/// depends on what is being held. Neither question can be answered by this table
/// alone any more, and a header that cannot see `kItems` cannot answer them at all.
/// The `drops` field stays here because it is a property of the block; resolving the
/// name is an item question.

static_assert(blockIdOf("air") == kAirBlock,
              "air must be the first entry in kBlocks; core/Types.hpp fixes it at 0");
static_assert(kBlocks.size() <= 256,
              "past 256 block types the palette's 8-bit index width no longer covers "
              "a section that holds one of everything");

/// A harvest tier with no tool kind can never be satisfied.
///
/// `canHarvest` asks whether the held tool's kind matches the block's *and* its tier
/// is high enough. A block demanding `ToolTier::Stone` while naming
/// `ToolKind::None` matches nothing, so it would be unbreakable-for-drops in a way
/// that looks like a mining bug rather than like a table typo. Two fields that only
/// mean something together, which is exactly the shape a static_assert is for.
consteval bool everyTierHasAKind() {
    for (const BlockInfo& block : kBlocks) {
        if (block.minTier != ToolTier::None && block.tool == ToolKind::None) {
            return false;
        }
    }
    return true;
}

static_assert(everyTierHasAKind(),
              "a block sets minTier without setting tool, so nothing can ever harvest it");

} // namespace mc
