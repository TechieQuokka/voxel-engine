#include "render/BlockTextures.hpp"

#include "core/Log.hpp"
#include "world/BlockTable.hpp"

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
        }
    }

    // The colours are sRGB-encoded, which is what an authored PNG would be too, so
    // the texture must declare itself as such.
    m_texture = rhi::TextureArray::create(kSize, kTextureLayerCount, pixels,
                                          rhi::ColorSpace::Srgb8A8);

    logDebug("Generated {} block textures at {}x{}", kTextureLayerCount, kSize, kSize);
}

} // namespace mc
