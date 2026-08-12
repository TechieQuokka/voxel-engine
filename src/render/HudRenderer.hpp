#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "render/InventoryLayout.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/Texture.hpp"
#include "rhi/VertexArray.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace mc {

class Inventory;

/// Everything drawn in screen space: the crosshair, the hotbar, the hearts, and the
/// inventory window.
///
/// **It used to say, in this comment, that it was deliberately not a UI framework.**
/// That was true and it stopped being enough. The count-based inventory it was built
/// against was replaced because carrying things is not felt as a number going up, and
/// a container needs a window, a cursor and hit testing -- the three things this was
/// written to avoid needing. It is a small UI layer now.
///
/// What it still is not is a *general* one. There is no widget tree, no layout
/// engine and no event routing: there is one window, its geometry lives in
/// `InventoryLayout`, and clicks are resolved by asking that layout which slot a
/// point is in. A second window would be the moment to reconsider, and a second
/// window is what a chest or a crafting bench would be.
///
/// Drawn in NDC with no projection at all, one draw call, one shader, a mode per
/// quad. The glyph font is hard-coded bit patterns expanded into a texture array at
/// startup -- ten digits and two hearts -- which keeps the rule that no binary asset
/// ships with this repository.
class HudRenderer {
public:
    HudRenderer();

    /// Everything the HUD needs that is not the inventory itself.
    struct State {
        /// Which hotbar slot is selected, 0-8.
        usize hotbarSlot = 0;

        /// In half-hearts, as vanilla counts them: 20 is ten full hearts.
        f32 health = 20.0f;
        f32 maxHealth = 20.0f;

        /// Where the crosshair goes, in NDC. **Not always the centre**: in third
        /// person the frame is drawn from over the player's shoulder while the aim
        /// ray is still cast from their eye, so the point being aimed at projects
        /// off-centre. The crosshair marks where the ray lands, not where the screen
        /// happens to be.
        f32 aimX = 0.0f;
        f32 aimY = 0.0f;

        /// Name of the block under the crosshair, or empty when nothing is targeted.
        /// Drawn under the crosshair, uppercased, underscores as spaces.
        std::string_view targetName;

        bool inventoryOpen = false;
        /// Where the pointer is, in NDC. Only read while the window is open, which
        /// is the only time there is a pointer to read.
        f32 cursorX = 0.0f;
        f32 cursorY = 0.0f;
    };

    void draw(rhi::Device& device, const BlockTextures& textures,
              const Inventory& inventory, const State& state, f32 aspect);

private:
    /// Layer of 'A' in the glyph atlas. Public because the atlas is built by a free
    /// function in the translation unit rather than by a member.
public:
    static constexpr u32 kFirstLetterGlyph = 12;

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

    /// Room for the open window: a panel, 36 slots each with a plate, an icon and up
    /// to two shadowed digits, the hearts and the dragged stack. About 400 in the
    /// worst case, and the buffer is 24 KiB either way.
    static constexpr usize kMaxQuads = 1024;
    static constexpr u32 kVerticesPerQuad = 6;
    static constexpr u32 kQuadBufferBinding = 0;
    static constexpr u32 kBlockTextureUnit = 0;
    static constexpr u32 kGlyphTextureUnit = 1;

    /// Ten digits, then the two heart shapes. The empty heart is the full one drawn
    /// dark rather than a third pattern, because a socket and a heart are the same
    /// silhouette and drawing them from one glyph is what keeps them aligned.
    static constexpr u32 kDigitGlyphs = 10;
    static constexpr u32 kHeartFullGlyph = 10;
    static constexpr u32 kHeartHalfGlyph = 11;
    static constexpr u32 kHeartGlyphs = 2;
    /// A-Z follow the hearts, so `kFirstLetterGlyph + (c - 'A')` is a letter's layer.
    static constexpr u32 kLetterCount = 26;
    static constexpr u32 kGlyphCount = kDigitGlyphs + kHeartGlyphs + kLetterCount;
    static constexpr u32 kGlyphSize = 8;

    /// Half-hearts per heart, which is what makes ten hearts show twenty health.
    static constexpr u32 kHealthPerHeart = 2;

    void push(const vec4& rect, const vec4& tint, Mode mode, u32 layer = 0);
    /// Lays out `value` right-aligned inside the given slot rect.
    void pushNumber(u32 value, const vec4& slot, f32 aspect);
    /// A slot's backing plate, its block icon and its count. Shared by the hotbar and
    /// the window, so a slot looks the same wherever it is drawn.
    void pushSlot(const UiRect& rect, const struct ItemStack& stack, bool highlight,
                  f32 aspect);
    void pushHearts(const InventoryLayout& layout, const State& state, f32 aspect);
    /// Draws `text` centred on `centreX`, with its top at `topY`, in NDC. Letters
    /// only: anything that is not a-z or A-Z advances the cursor as a space, which
    /// is what turns `oak_log` into `OAK LOG` without a second table.
    void pushText(std::string_view text, f32 centreX, f32 topY, f32 height, f32 aspect);

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<rhi::Buffer> m_buffer;
    std::optional<rhi::TextureArray> m_glyphs;

    std::vector<GpuQuad> m_quads;
};

} // namespace mc
