#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/Texture.hpp"
#include "rhi/VertexArray.hpp"

#include <optional>
#include <span>
#include <vector>

namespace mc {

class Inventory;

/// The hotbar, the counts on it, and the crosshair.
///
/// **The engine's first screen-space layer.** Everything before it was in the world;
/// this is drawn in normalised device coordinates with no projection at all, because
/// the only transform a HUD needs is the one the CPU already did when it laid the
/// slots out.
///
/// It is deliberately not a UI framework. There is no cursor mode, no hit testing
/// and no window -- those are what a slot-based inventory would need, and the count
/// model (see `Inventory`) exists precisely so they are not needed yet.
///
/// The digit font is nine hundred bits of hard-coded 3x5 patterns, expanded into a
/// texture array at startup. That keeps the repository's rule that no binary asset
/// ships with it, and a font that only has to render "0" to "999" does not justify
/// more.
class HudRenderer {
public:
    HudRenderer();

    /// `slots` is what the hotbar holds, `selected` which one is active, and
    /// `aspect` the framebuffer's width over its height -- without it the slots come
    /// out as rectangles on any window that is not square.
    void draw(rhi::Device& device, const BlockTextures& textures,
              std::span<const BlockId> slots, usize selected,
              const Inventory& inventory, f32 aspect);

private:
    /// Matches the mode constants in hud.frag.
    enum class Mode : u32 {
        Solid = 0,
        Block = 1,
        Glyph = 2,
    };

    /// Matches `HudQuad` in hud.vert.
    struct GpuQuad {
        vec4 rect;
        vec4 tint;
        vec4 params;
    };

    static constexpr usize kMaxQuads = 256;
    static constexpr u32 kVerticesPerQuad = 6;
    static constexpr u32 kQuadBufferBinding = 0;
    static constexpr u32 kBlockTextureUnit = 0;
    static constexpr u32 kGlyphTextureUnit = 1;

    /// Digits only. Ten glyphs is all a count needs.
    static constexpr u32 kGlyphCount = 10;
    static constexpr u32 kGlyphSize = 8;

    /// Slot height as a fraction of the screen height, and the gap between slots.
    static constexpr f32 kSlotHeight = 0.11f;
    static constexpr f32 kSlotGap = 0.012f;
    /// Distance from the bottom edge to the bottom of the hotbar.
    static constexpr f32 kBottomMargin = 0.04f;

    void push(const vec4& rect, const vec4& tint, Mode mode, u32 layer = 0);
    /// Lays out `value` right-aligned inside the given slot rect.
    void pushNumber(u32 value, const vec4& slot, f32 aspect);

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<rhi::Buffer> m_buffer;
    std::optional<rhi::TextureArray> m_glyphs;

    std::vector<GpuQuad> m_quads;
};

} // namespace mc
