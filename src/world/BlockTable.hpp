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
};

/// Block types, in BlockId order. The index of an entry *is* its BlockId.
///
/// Air must stay first: `kAirBlock` is 0 in core/Types.hpp, where the mesher and
/// the palette both need it without knowing this table exists. The static_assert
/// below is what keeps the two from drifting.
inline constexpr std::array kBlocks{
    BlockInfo{"air",   false, layerOf("stone"), layerOf("stone"), layerOf("stone"), '.'},
    BlockInfo{"stone", true,  layerOf("stone"), layerOf("stone"), layerOf("stone"), '#'},
    BlockInfo{"dirt",  true,  layerOf("dirt"),  layerOf("dirt"),  layerOf("dirt"),  'd'},
    // Grass is the reason a face carries a layer rather than a block id: one block
    // type, three different textures.
    BlockInfo{"grass", true,  layerOf("grass_top"), layerOf("grass_side"),
                              layerOf("dirt"),  'g'},
    BlockInfo{"sand",  true,  layerOf("sand"),  layerOf("sand"),  layerOf("sand"),  's'},

    BlockInfo{"bedrock",   true, layerOf("bedrock"),   layerOf("bedrock"),
                                 layerOf("bedrock"),   'B'},
    BlockInfo{"deepslate", true, layerOf("deepslate"), layerOf("deepslate"),
                                 layerOf("deepslate"), 'D'},
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

static_assert(blockIdOf("air") == kAirBlock,
              "air must be the first entry in kBlocks; core/Types.hpp fixes it at 0");
static_assert(kBlocks.size() <= 256,
              "past 256 block types the palette's 8-bit index width no longer covers "
              "a section that holds one of everything");

} // namespace mc
