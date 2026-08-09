#include "render/BlockTextures.hpp"

#include "core/Log.hpp"
#include "world/BlockRegistry.hpp"

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

} // namespace

BlockTextures::BlockTextures() {
    const BlockRegistry& registry = BlockRegistry::instance();

    const u32 dirtArgb = registry[kDirtBlock].debugColor;
    const u32 grassArgb = registry[kGrassBlock].debugColor;

    std::vector<u8> pixels(static_cast<usize>(kSize) * kSize * kTextureLayerCount * 4, 0);

    generateGrain(pixels, static_cast<u32>(TextureLayer::Stone),
                  registry[kStoneBlock].debugColor, 20.0f, 1u);
    generateGrain(pixels, static_cast<u32>(TextureLayer::Dirt), dirtArgb, 18.0f, 2u);
    generateGrain(pixels, static_cast<u32>(TextureLayer::GrassTop), grassArgb, 16.0f, 3u);
    generateGrassSide(pixels, static_cast<u32>(TextureLayer::GrassSide), dirtArgb, grassArgb);
    generateGrain(pixels, static_cast<u32>(TextureLayer::Sand),
                  registry[kSandBlock].debugColor, 10.0f, 5u);

    m_texture = rhi::TextureArray::create(kSize, kTextureLayerCount, pixels);

    logDebug("Generated {} block textures at {}x{}", kTextureLayerCount, kSize, kSize);
}

} // namespace mc
