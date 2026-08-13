#include "render/BlockTextures.hpp"

#include "core/Log.hpp"
#include "world/BlockTable.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace mc {
namespace {

constexpr u32 kSize = BlockTextures::kTextureSize;

/// Deterministic value hash. A real noise library is overkill for 16x16
/// placeholder textures, and worldgen's FastNoise2 dependency has no business
/// being pulled into the renderer.
u32 hash2D(u32 x, u32 y, u32 seed) {
    u32 h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/// Signed noise in roughly [-1, 1].
f32 noise(u32 x, u32 y, u32 seed) {
    return static_cast<f32>(hash2D(x, y, seed) & 0xFFFFu) / 32767.5f - 1.0f;
}

struct Rgba {
    u8 r, g, b, a;
};

constexpr Rgba fromArgb(u32 argb) {
    return {static_cast<u8>((argb >> 16) & 0xFFu),
            static_cast<u8>((argb >> 8) & 0xFFu),
            static_cast<u8>(argb & 0xFFu),
            static_cast<u8>((argb >> 24) & 0xFFu)};
}

u8 clampChannel(f32 value) {
    return static_cast<u8>(std::lround(std::fmin(std::fmax(value, 0.0f), 255.0f)));
}

Rgba shade(Rgba base, f32 amount) {
    return {clampChannel(static_cast<f32>(base.r) + amount),
            clampChannel(static_cast<f32>(base.g) + amount),
            clampChannel(static_cast<f32>(base.b) + amount),
            base.a};
}

void writePixel(std::vector<u8>& out, u32 layer, u32 x, u32 y, Rgba color) {
    const usize index = ((static_cast<usize>(layer) * kSize + y) * kSize + x) * 4;
    out[index + 0] = color.r;
    out[index + 1] = color.g;
    out[index + 2] = color.b;
    out[index + 3] = color.a;
}

/// Uniform grain. Works for stone, dirt and sand, which differ only in base
/// colour and how rough they are.
void generateGrain(std::vector<u8>& out, u32 layer, u32 argb, f32 roughness, u32 seed) {
    const Rgba base = fromArgb(argb);
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const f32 n = noise(x, y, seed) * roughness
                        + noise(x / 2, y / 2, seed + 7) * roughness * 0.5f;
            writePixel(out, layer, x, y, shade(base, n));
        }
    }
}

/// Host rock, with a scatter of ore blobs over it.
///
/// Blob centres come from the layer's own seed, so every ore gets a different
/// arrangement rather than the same stencil in a different colour -- which is what
/// it looked like when they shared one. The blobs are drawn with a darkened rim so
/// they read as inclusions in the rock rather than as paint on top of it.
void generateOre(std::vector<u8>& out, u32 layer, u32 rockArgb, u32 oreArgb,
                 f32 roughness, u32 seed) {
    generateGrain(out, layer, rockArgb, roughness, seed);

    const Rgba ore = fromArgb(oreArgb);
    constexpr u32 kBlobCount = 7;

    for (u32 blob = 0; blob < kBlobCount; ++blob) {
        const u32 cx = hash2D(blob, 0u, seed) % kSize;
        const u32 cy = hash2D(blob, 1u, seed) % kSize;
        // Two or three pixels across. Bigger reads as a solid block of ore.
        const f32 radius = 1.6f + static_cast<f32>(hash2D(blob, 2u, seed) % 100u) / 120.0f;

        const auto span = static_cast<i32>(radius) + 1;
        for (i32 dy = -span; dy <= span; ++dy) {
            for (i32 dx = -span; dx <= span; ++dx) {
                const f32 distance = std::sqrt(static_cast<f32>(dx * dx + dy * dy));
                if (distance > radius) {
                    continue;
                }
                // Wrap, so a blob near an edge continues on the far side and the
                // texture still tiles across a merged quad.
                const u32 x = (cx + static_cast<u32>(dx + static_cast<i32>(kSize))) % kSize;
                const u32 y = (cy + static_cast<u32>(dy + static_cast<i32>(kSize))) % kSize;

                const f32 rim = distance > radius - 1.0f ? -34.0f : 0.0f;
                writePixel(out, layer, x, y,
                           shade(ore, rim + noise(x, y, seed + 3u) * 10.0f));
            }
        }
    }
}

/// Dirt with a grass fringe along the top edge, so the side of a grass block
/// reads correctly where it meets the top face.
void generateGrassSide(std::vector<u8>& out, u32 layer, u32 dirtArgb, u32 grassArgb) {
    const Rgba dirt = fromArgb(dirtArgb);
    const Rgba grass = fromArgb(grassArgb);

    for (u32 x = 0; x < kSize; ++x) {
        // Ragged boundary rather than a straight line.
        const u32 fringe = 3u + (hash2D(x, 0, 91u) % 3u);
        for (u32 y = 0; y < kSize; ++y) {
            const bool isGrass = y < fringe;
            const Rgba base = isGrass ? grass : dirt;
            const f32 n = noise(x, y, isGrass ? 11u : 23u) * 14.0f;
            writePixel(out, layer, x, y, shade(base, n));
        }
    }
}

/// The cut end of a log: concentric growth rings around an off-centre heart.
///
/// Off-centre on purpose. Rings centred in the tile give a bullseye, which reads as a
/// target painted on the block; a real trunk's heart is never quite in the middle,
/// and moving it two pixels is the whole difference.
void generateRings(std::vector<u8>& out, u32 layer, u32 woodArgb, u32 ringArgb,
                   f32 roughness, u32 seed) {
    const Rgba wood = fromArgb(woodArgb);
    const Rgba ring = fromArgb(ringArgb);

    constexpr f32 kCenterX = 7.0f;
    constexpr f32 kCenterY = 6.0f;

    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const f32 dx = static_cast<f32>(x) - kCenterX;
            const f32 dy = static_cast<f32>(y) - kCenterY;
            // The wobble is what stops the rings being perfect circles.
            const f32 radius = std::sqrt(dx * dx + dy * dy) + noise(x, y, seed) * 0.9f;

            const bool onRing = std::fmod(radius, 2.6f) < 1.15f;
            const Rgba base = onRing ? ring : wood;
            writePixel(out, layer, x, y, shade(base, noise(x, y, seed + 5u) * roughness));
        }
    }
}

/// Bark: streaks that run the height of the tile rather than isotropic noise.
///
/// A log's side sampled with plain grain looks like dirt. What says "trunk" is that
/// the variation is vertical, so the noise is stretched hard along y -- neighbouring
/// rows share a value and neighbouring columns do not.
void generateBark(std::vector<u8>& out, u32 layer, u32 barkArgb, u32 grooveArgb,
                  f32 roughness, u32 seed) {
    const Rgba bark = fromArgb(barkArgb);
    const Rgba groove = fromArgb(grooveArgb);

    for (u32 x = 0; x < kSize; ++x) {
        // One decision per column: is this a groove, and how dark is the whole streak.
        const bool groovy = (hash2D(x, 0u, seed) % 5u) == 0u;
        const f32 columnShade = noise(x, 0u, seed + 3u) * roughness;

        for (u32 y = 0; y < kSize; ++y) {
            const Rgba base = groovy ? groove : bark;
            // y varies four times slower than x, which is the stretch.
            const f32 n = noise(x, y / 4u, seed + 9u) * roughness * 0.5f;
            writePixel(out, layer, x, y, shade(base, columnShade + n));
        }
    }
}

/// Sawn boards: horizontal bands with a dark seam between them, and a vertical seam
/// inside each band at a different column.
///
/// The seams are the whole texture. Grain in a plank colour reads as pale dirt; what
/// says "board" is a straight line every few rows and the fact that the short joints
/// do not line up between them.
void generatePlanks(std::vector<u8>& out, u32 layer, u32 plankArgb, u32 seamArgb,
                    f32 roughness, u32 seed) {
    const Rgba plank = fromArgb(plankArgb);
    const Rgba seam = fromArgb(seamArgb);

    constexpr u32 kBoardHeight = 4;

    for (u32 y = 0; y < kSize; ++y) {
        const u32 board = y / kBoardHeight;
        const bool horizontalSeam = (y % kBoardHeight) == 0;
        // One short joint per board, at a column that varies between them. Lining
        // them up would read as a grid rather than as staggered planks.
        const u32 joint = 3u + (hash2D(board, 0u, seed) % 10u);

        for (u32 x = 0; x < kSize; ++x) {
            const bool verticalSeam = x == joint;
            const Rgba base = horizontalSeam || verticalSeam ? seam : plank;
            // Grain runs along the board, so x varies faster than y -- the opposite
            // of bark, and for the same reason: the direction is the information.
            const f32 n = noise(x, y / 2u, seed + board) * roughness;
            writePixel(out, layer, x, y, shade(base, n));
        }
    }
}

// -- item icons ------------------------------------------------------------------
//
// **Shapes are hard-coded bit patterns, exactly as the HUD's font is.** That is
// already the established answer in this repository for "a small picture with no
// binary asset", and reusing it means an icon is readable in the source: each row
// below is sixteen characters that look like the row they draw.
//
// Bit 15 is column 0, so a binary literal reads left to right as the image does.
// Getting that backwards mirrors every tool and looks like a UV bug.

using IconMask = std::array<u16, kSize>;

bool maskBit(u16 row, u32 x) {
    // All-unsigned on purpose. `row >> n & 1u` on a u16 promotes to int and converts
    // back, which is a -Wsign-conversion error under this project's warning set --
    // and is the exact bug HANDOFF.md 5 records the HUD's digit blitter shipping.
    return ((static_cast<u32>(row) >> (15u - x)) & 1u) != 0u;
}

/// Draws every set bit of `mask` in `color`, leaving the rest of the tile untouched.
/// Untouched means transparent: the pixel buffer starts zeroed, and alpha 0 is what
/// makes an icon a shape rather than a square.
void drawMask(std::vector<u8>& out, u32 layer, const IconMask& mask, u32 argb,
              f32 roughness, u32 seed) {
    const Rgba base = fromArgb(argb);
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            if (!maskBit(mask[y], x)) {
                continue;
            }
            writePixel(out, layer, x, y, shade(base, noise(x, y, seed) * roughness));
        }
    }
}

/// The shaft every tool shares, running from under the head down to the lower left.
constexpr IconMask kHandleArt{
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000011000000, 0b0000000110000000, 0b0000001100000000,
    0b0000011000000000, 0b0000110000000000, 0b0001100000000000, 0b0011000000000000,
    0b0110000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

constexpr IconMask kPickaxeHeadArt{
    0b0000000000000000, 0b0000000000000000, 0b0000111111111000, 0b0001111111111100,
    0b0001100000011000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

constexpr IconMask kAxeHeadArt{
    0b0000000000000000, 0b0000000000000000, 0b0000000111110000, 0b0000001111111000,
    0b0000001111111000, 0b0000001111110000, 0b0000000111100000, 0b0000000111000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

constexpr IconMask kShovelHeadArt{
    0b0000000000000000, 0b0000000000000000, 0b0000001111000000, 0b0000001111000000,
    0b0000001111000000, 0b0000001110000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

/// A sword is a blade rather than a head on a stick, so it does not use the shared
/// handle: the grip runs on past the guard where a tool's shaft simply stops.
constexpr IconMask kSwordBladeArt{
    0b0000000000000000, 0b0000000000111000, 0b0000000001110000, 0b0000000011100000,
    0b0000000111000000, 0b0000001110000000, 0b0000011100000000, 0b0000111000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

constexpr IconMask kSwordGripArt{
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
    0b0001111110000000, 0b0001100000000000, 0b0011000000000000, 0b0110000000000000,
    0b1110000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

constexpr IconMask kStickArt{
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000110000,
    0b0000000001100000, 0b0000000011000000, 0b0000000110000000, 0b0000001100000000,
    0b0000011000000000, 0b0000110000000000, 0b0001100000000000, 0b0011000000000000,
    0b0000000000000000, 0b0000000000000000, 0b0000000000000000, 0b0000000000000000,
};

/// A tool: the shared shaft in the handle colour, then the head over it in the tier
/// colour. Head second so an overlapping pixel belongs to the head, which is what
/// makes the joint read as the head being mounted rather than the shaft passing
/// through it.
void generateTool(std::vector<u8>& out, u32 layer, const IconMask& head, u32 headArgb,
                  u32 handleArgb, f32 roughness, u32 seed) {
    drawMask(out, layer, kHandleArt, handleArgb, roughness * 0.6f, seed + 1u);
    drawMask(out, layer, head, headArgb, roughness, seed);
}

void generateSword(std::vector<u8>& out, u32 layer, u32 bladeArgb, u32 gripArgb,
                   f32 roughness, u32 seed) {
    drawMask(out, layer, kSwordGripArt, gripArgb, roughness * 0.6f, seed + 1u);
    drawMask(out, layer, kSwordBladeArt, bladeArgb, roughness, seed);
}

/// A rounded lump in the middle of the tile, rim-shaded so it reads as solid.
///
/// Generated rather than masked, because the four things that use it differ only in
/// colour and a circle is shorter written than drawn. The wobble on the radius is
/// what stops four identical discs.
void generateNugget(std::vector<u8>& out, u32 layer, u32 bodyArgb, u32 rimArgb,
                    f32 roughness, u32 seed) {
    const Rgba body = fromArgb(bodyArgb);
    const Rgba rim = fromArgb(rimArgb);

    constexpr f32 kCenter = 7.5f;
    constexpr f32 kRadius = 4.6f;

    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            const f32 dx = static_cast<f32>(x) - kCenter;
            const f32 dy = static_cast<f32>(y) - kCenter;
            const f32 distance =
                std::sqrt(dx * dx + dy * dy) + noise(x, y, seed) * 0.75f;
            if (distance > kRadius) {
                continue; // Outside the lump: left transparent.
            }
            const Rgba base = distance > kRadius - 1.2f ? rim : body;
            writePixel(out, layer, x, y, shade(base, noise(x, y, seed + 4u) * roughness));
        }
    }
}

} // namespace

BlockTextures::BlockTextures() {
    std::vector<u8> pixels(static_cast<usize>(kSize) * kSize * kTextureLayerCount * 4, 0);

    // One pass over the layer table, in table order -- an entry's position in
    // kLayers is its array layer, so nothing here assigns an index. Adding a layer
    // is an entry in that table and no edit at all in this file, which is the
    // fourth place a new block type used to have to be written down.
    //
    // The switch is deliberately unguarded by a default: a new TextureRecipe then
    // fails to compile here rather than silently generating a blank layer.
    for (usize i = 0; i < kLayers.size(); ++i) {
        const LayerInfo& layer = kLayers[i];
        const auto index = static_cast<u32>(i);

        switch (layer.recipe) {
        case TextureRecipe::Grain:
            generateGrain(pixels, index, layer.argb, layer.roughness, layer.seed);
            break;
        case TextureRecipe::GrassSide:
            generateGrassSide(pixels, index, layer.argb, layer.argbSecondary);
            break;
        case TextureRecipe::Ore:
            generateOre(pixels, index, layer.argb, layer.argbSecondary,
                        layer.roughness, layer.seed);
            break;
        case TextureRecipe::Rings:
            generateRings(pixels, index, layer.argb, layer.argbSecondary,
                          layer.roughness, layer.seed);
            break;
        case TextureRecipe::Bark:
            generateBark(pixels, index, layer.argb, layer.argbSecondary,
                         layer.roughness, layer.seed);
            break;
        case TextureRecipe::Planks:
            generatePlanks(pixels, index, layer.argb, layer.argbSecondary,
                           layer.roughness, layer.seed);
            break;
        case TextureRecipe::Nugget:
            generateNugget(pixels, index, layer.argb, layer.argbSecondary,
                           layer.roughness, layer.seed);
            break;
        case TextureRecipe::Stick:
            drawMask(pixels, index, kStickArt, layer.argb, layer.roughness, layer.seed);
            break;
        case TextureRecipe::ToolPickaxe:
            generateTool(pixels, index, kPickaxeHeadArt, layer.argb,
                         layer.argbSecondary, layer.roughness, layer.seed);
            break;
        case TextureRecipe::ToolAxe:
            generateTool(pixels, index, kAxeHeadArt, layer.argb, layer.argbSecondary,
                         layer.roughness, layer.seed);
            break;
        case TextureRecipe::ToolShovel:
            generateTool(pixels, index, kShovelHeadArt, layer.argb,
                         layer.argbSecondary, layer.roughness, layer.seed);
            break;
        case TextureRecipe::ToolSword:
            generateSword(pixels, index, layer.argb, layer.argbSecondary,
                          layer.roughness, layer.seed);
            break;
        }
    }

    // The colours are sRGB-encoded, which is what an authored PNG would be too, so
    // the texture must declare itself as such.
    m_texture = rhi::TextureArray::create(kSize, kTextureLayerCount, pixels,
                                          rhi::ColorSpace::Srgb8A8);

    logDebug("Generated {} block textures at {}x{}", kTextureLayerCount, kSize, kSize);
}

} // namespace mc
