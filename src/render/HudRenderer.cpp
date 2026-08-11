#include "render/HudRenderer.hpp"

#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "world/Inventory.hpp"

#include <array>
#include <cstddef>

namespace mc {
namespace {

/// 3x5 digit patterns, one bit per pixel, row-major from the top. Bit 2 is the
/// leftmost column of a row, so a row reads left to right as written.
///
/// Hand-coded because ten glyphs is not worth a font file, a parser, or the rule
/// that this repository ships no binary assets being broken for it.
constexpr std::array<u16, 10> kDigits{{
    0b111'101'101'101'111u, // 0
    0b010'110'010'010'111u, // 1
    0b111'001'111'100'111u, // 2
    0b111'001'111'001'111u, // 3
    0b101'101'111'001'001u, // 4
    0b111'100'111'001'111u, // 5
    0b111'100'111'101'111u, // 6
    0b111'001'001'001'001u, // 7
    0b111'101'111'101'111u, // 8
    0b111'101'111'001'111u, // 9
}};

constexpr u32 kDigitWidth = 3;
constexpr u32 kDigitHeight = 5;

/// Expands the patterns into an 8x8 RGBA array, white with coverage in the alpha.
/// The colour comes from the tint at draw time, so one texture serves any colour.
std::vector<u8> buildGlyphAtlas(u32 size, u32 count) {
    std::vector<u8> pixels(static_cast<usize>(size) * size * count * 4, 0);

    // Centred, with a pixel of margin, so digits sit evenly next to each other.
    const u32 originX = (size - kDigitWidth) / 2;
    const u32 originY = (size - kDigitHeight) / 2;

    for (u32 glyph = 0; glyph < count; ++glyph) {
        for (u32 row = 0; row < kDigitHeight; ++row) {
            for (u32 column = 0; column < kDigitWidth; ++column) {
                const u32 bit = (kDigitHeight - 1 - row) * kDigitWidth
                              + (kDigitWidth - 1 - column);
                if ((kDigits[glyph] >> bit & 1u) == 0u) {
                    continue;
                }
                const usize index =
                    ((static_cast<usize>(glyph) * size + originY + row) * size
                     + originX + column) * 4;
                pixels[index + 0] = 255;
                pixels[index + 1] = 255;
                pixels[index + 2] = 255;
                pixels[index + 3] = 255;
            }
        }
    }

    return pixels;
}

} // namespace

HudRenderer::HudRenderer()
    : m_shader(rhi::Shader::fromFiles(assetPath("shaders/hud.vert"),
                                      assetPath("shaders/hud.frag"))) {
    const std::vector<u8> glyphs = buildGlyphAtlas(kGlyphSize, kGlyphCount);
    // Rgba8, not sRGB: this is a coverage mask, not a colour. Decoding it would
    // change the alpha threshold the fragment shader tests against.
    m_glyphs = rhi::TextureArray::create(kGlyphSize, kGlyphCount, glyphs,
                                         rhi::ColorSpace::Rgba8);

    m_shader.setUniform("u_blockTextures", static_cast<i32>(kBlockTextureUnit));
    m_shader.setUniform("u_glyphs", static_cast<i32>(kGlyphTextureUnit));

    m_quads.reserve(kMaxQuads);
    m_buffer = rhi::Buffer::createPersistent(kMaxQuads * sizeof(GpuQuad));
}

void HudRenderer::push(const vec4& rect, const vec4& tint, Mode mode, u32 layer) {
    if (m_quads.size() >= kMaxQuads) {
        return;
    }
    m_quads.push_back(GpuQuad{rect, tint,
                              vec4{static_cast<f32>(mode), static_cast<f32>(layer),
                                   0.0f, 0.0f}});
}

void HudRenderer::pushNumber(u32 value, const vec4& slot, f32 aspect) {
    // Digits are drawn right to left from the slot's bottom-right corner, which is
    // where Minecraft puts a stack count and where the eye already looks for one.
    const f32 height = (slot.w - slot.y) * 0.42f;
    const f32 width = height / aspect * 0.8f;

    f32 x = slot.z - width * 0.15f;
    const f32 y = slot.y + (slot.w - slot.y) * 0.06f;

    u32 remaining = value;
    do {
        const u32 digit = remaining % 10u;
        remaining /= 10u;

        const vec4 rect{x - width, y, x, y + height};
        // A hard shadow one pixel down and right, because a white digit over a light
        // block texture is otherwise unreadable.
        const f32 shadow = height * 0.12f;
        push(vec4{rect.x + shadow / aspect, rect.y - shadow,
                  rect.z + shadow / aspect, rect.w - shadow},
             vec4{0.0f, 0.0f, 0.0f, 0.85f}, Mode::Glyph, digit);
        push(rect, vec4{1.0f, 1.0f, 1.0f, 1.0f}, Mode::Glyph, digit);

        x -= width;
    } while (remaining > 0);
}

void HudRenderer::draw(rhi::Device& device, const BlockTextures& textures,
                       std::span<const BlockId> slots, usize selected,
                       const Inventory& inventory, f32 aspect) {
    MC_PROFILE_SCOPE_N("HudRenderer::draw");

    m_quads.clear();

    // ---------------------------------------------------------------------------
    // Crosshair. Two thin bars rather than a texture -- it is four triangles and it
    // has to be exactly centred, which arithmetic gets right and a sprite does not.
    // ---------------------------------------------------------------------------
    constexpr f32 kArm = 0.018f;
    constexpr f32 kThickness = 0.0022f;
    const vec4 crosshairTint{1.0f, 1.0f, 1.0f, 0.75f};
    push(vec4{-kArm / aspect, -kThickness, kArm / aspect, kThickness}, crosshairTint,
         Mode::Solid);
    push(vec4{-kThickness / aspect, -kArm, kThickness / aspect, kArm}, crosshairTint,
         Mode::Solid);

    // ---------------------------------------------------------------------------
    // Hotbar.
    // ---------------------------------------------------------------------------
    const f32 slotWidth = kSlotHeight / aspect;
    const f32 gap = kSlotGap / aspect;
    const f32 totalWidth = static_cast<f32>(slots.size()) * slotWidth
                         + static_cast<f32>(slots.size() - 1) * gap;

    f32 x = -totalWidth * 0.5f;
    const f32 y = -1.0f + kBottomMargin;

    for (usize i = 0; i < slots.size(); ++i) {
        const vec4 slot{x, y, x + slotWidth, y + kSlotHeight};
        const bool active = i == selected;

        // Backing plate, brighter for the selected slot. Drawn slightly larger than
        // the icon so it reads as a frame around it.
        const f32 border = slotWidth * 0.09f;
        push(vec4{slot.x - border, slot.y - border, slot.z + border, slot.w + border},
             active ? vec4{1.0f, 1.0f, 1.0f, 0.55f} : vec4{0.0f, 0.0f, 0.0f, 0.38f},
             Mode::Solid);

        const BlockId block = slots[i];
        const u32 held = inventory.count(block);

        // An empty slot's icon is dimmed rather than hidden, so the hotbar keeps its
        // shape and the player can see what a slot *would* place.
        const f32 brightness = held > 0 ? 1.0f : 0.32f;
        push(slot, vec4{brightness, brightness, brightness, 1.0f}, Mode::Block,
             kBlocks[block].top);

        if (held > 0) {
            pushNumber(held, slot, aspect);
        }

        x += slotWidth + gap;
    }

    if (m_quads.empty()) {
        return;
    }

    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(m_quads.data()), m_quads.size() * sizeof(GpuQuad)};
    m_buffer->write(0, bytes);
    rhi::Buffer::barrierAfterClientWrites();

    m_shader.bind();
    textures.bind(kBlockTextureUnit);
    m_glyphs->bind(kGlyphTextureUnit);
    m_buffer->bindBase(rhi::BufferTarget::Storage, kQuadBufferBinding);
    m_vao.bind();

    // Over everything, and depth-testing against nothing. Restored afterwards so the
    // next frame's world draw starts from the state it expects.
    device.setDepthTest(false);
    device.setAlphaBlending(true);
    device.drawTriangles(static_cast<u32>(m_quads.size()) * kVerticesPerQuad);
    device.setAlphaBlending(false);
    device.setDepthTest(true);
}

} // namespace mc
