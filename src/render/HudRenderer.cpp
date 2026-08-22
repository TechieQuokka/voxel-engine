#include "render/HudRenderer.hpp"

#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "world/BlockShape.hpp"
#include "world/BlockTable.hpp"
#include "world/ItemTable.hpp"
#include "world/Inventory.hpp"
#include "world/Screen.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

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

/// A 7x7 heart, row-major from the top, bit 6 leftmost.
///
/// Larger than the digits because it is read at a glance rather than deciphered --
/// a 3x5 heart is a blob. Same technique though: bits in the source, no asset.
constexpr u32 kHeartWidth = 7;
constexpr u32 kHeartHeight = 7;
constexpr std::array<u8, kHeartHeight> kHeart{{
    0b0110110u,
    0b1111111u,
    0b1111111u,
    0b1111111u,
    0b0111110u,
    0b0011100u,
    0b0001000u,
}};

/// 5x7 uppercase letters, row-major from the top, bit 4 leftmost.
///
/// **Added so the HUD can name the block under the crosshair.** In third person the
/// character stands between the camera and anything within arm's reach, so a player
/// mining a block at their feet cannot see it however the camera is placed -- the
/// name is what works when the picture does not.
///
/// 5x7 rather than the digits' 3x5: three columns cannot make a legible M or W, and
/// these are read as words rather than deciphered as figures. Hand-coded for the same
/// reason the digits are -- no font file, no parser, and no binary asset in the
/// repository.
constexpr u32 kLetterWidth = 5;
constexpr u32 kLetterHeight = 7;
constexpr std::array<std::array<u8, kLetterHeight>, 26> kLetters{{
    {{0b01110u, 0b10001u, 0b10001u, 0b11111u, 0b10001u, 0b10001u, 0b10001u}}, // A
    {{0b11110u, 0b10001u, 0b10001u, 0b11110u, 0b10001u, 0b10001u, 0b11110u}}, // B
    {{0b01110u, 0b10001u, 0b10000u, 0b10000u, 0b10000u, 0b10001u, 0b01110u}}, // C
    {{0b11110u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b11110u}}, // D
    {{0b11111u, 0b10000u, 0b10000u, 0b11110u, 0b10000u, 0b10000u, 0b11111u}}, // E
    {{0b11111u, 0b10000u, 0b10000u, 0b11110u, 0b10000u, 0b10000u, 0b10000u}}, // F
    {{0b01110u, 0b10001u, 0b10000u, 0b10111u, 0b10001u, 0b10001u, 0b01110u}}, // G
    {{0b10001u, 0b10001u, 0b10001u, 0b11111u, 0b10001u, 0b10001u, 0b10001u}}, // H
    {{0b01110u, 0b00100u, 0b00100u, 0b00100u, 0b00100u, 0b00100u, 0b01110u}}, // I
    {{0b00111u, 0b00010u, 0b00010u, 0b00010u, 0b00010u, 0b10010u, 0b01100u}}, // J
    {{0b10001u, 0b10010u, 0b10100u, 0b11000u, 0b10100u, 0b10010u, 0b10001u}}, // K
    {{0b10000u, 0b10000u, 0b10000u, 0b10000u, 0b10000u, 0b10000u, 0b11111u}}, // L
    {{0b10001u, 0b11011u, 0b10101u, 0b10101u, 0b10001u, 0b10001u, 0b10001u}}, // M
    {{0b10001u, 0b11001u, 0b10101u, 0b10011u, 0b10001u, 0b10001u, 0b10001u}}, // N
    {{0b01110u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b01110u}}, // O
    {{0b11110u, 0b10001u, 0b10001u, 0b11110u, 0b10000u, 0b10000u, 0b10000u}}, // P
    {{0b01110u, 0b10001u, 0b10001u, 0b10001u, 0b10101u, 0b10010u, 0b01101u}}, // Q
    {{0b11110u, 0b10001u, 0b10001u, 0b11110u, 0b10100u, 0b10010u, 0b10001u}}, // R
    {{0b01111u, 0b10000u, 0b10000u, 0b01110u, 0b00001u, 0b00001u, 0b11110u}}, // S
    {{0b11111u, 0b00100u, 0b00100u, 0b00100u, 0b00100u, 0b00100u, 0b00100u}}, // T
    {{0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b01110u}}, // U
    {{0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b10001u, 0b01010u, 0b00100u}}, // V
    {{0b10001u, 0b10001u, 0b10001u, 0b10101u, 0b10101u, 0b11011u, 0b10001u}}, // W
    {{0b10001u, 0b10001u, 0b01010u, 0b00100u, 0b01010u, 0b10001u, 0b10001u}}, // X
    {{0b10001u, 0b10001u, 0b01010u, 0b00100u, 0b00100u, 0b00100u, 0b00100u}}, // Y
    {{0b11111u, 0b00001u, 0b00010u, 0b00100u, 0b01000u, 0b10000u, 0b11111u}}, // Z
}};

/// Keeps the left four columns. A half heart is the full one cut down the middle
/// rather than a second drawing, so the two cannot drift apart by a pixel.
constexpr u8 kLeftHalfMask = 0b1111000u;

void blit(std::vector<u8>& pixels, u32 size, u32 glyph, u32 x, u32 y) {
    const usize index = ((static_cast<usize>(glyph) * size + y) * size + x) * 4;
    pixels[index + 0] = 255;
    pixels[index + 1] = 255;
    pixels[index + 2] = 255;
    pixels[index + 3] = 255;
}

/// Expands the patterns into an 8x8 RGBA array, white with coverage in the alpha.
/// The colour comes from the tint at draw time, so one texture serves any colour --
/// which is also how one heart glyph serves the red one and the dark socket under it.
std::vector<u8> buildGlyphAtlas(u32 size, u32 count) {
    std::vector<u8> pixels(static_cast<usize>(size) * size * count * 4, 0);

    // Centred, with a pixel of margin, so digits sit evenly next to each other.
    const u32 digitX = (size - kDigitWidth) / 2;
    const u32 digitY = (size - kDigitHeight) / 2;

    for (u32 glyph = 0; glyph < kDigits.size(); ++glyph) {
        for (u32 row = 0; row < kDigitHeight; ++row) {
            for (u32 column = 0; column < kDigitWidth; ++column) {
                const u32 bit = (kDigitHeight - 1 - row) * kDigitWidth
                              + (kDigitWidth - 1 - column);
                // Widened before the shift. `kDigits` is u16, so it promotes to a
                // signed int and `& 1u` then converts back -- which `-Wsign-conversion`
                // rejects, and rightly: the whole point of this bitmap is unsigned.
                if (((static_cast<u32>(kDigits[glyph]) >> bit) & 1u) == 0u) {
                    continue;
                }
                blit(pixels, size, glyph, digitX + column, digitY + row);
            }
        }
    }

    const u32 heartX = (size - kHeartWidth) / 2;
    const u32 heartY = (size - kHeartHeight) / 2;

    for (u32 half = 0; half < 2; ++half) {
        const u32 glyph = static_cast<u32>(kDigits.size()) + half;
        const u8 mask = half == 0 ? 0b1111111u : kLeftHalfMask;

        for (u32 row = 0; row < kHeartHeight; ++row) {
            const u8 bits = static_cast<u8>(kHeart[row] & mask);
            for (u32 column = 0; column < kHeartWidth; ++column) {
                if ((bits >> (kHeartWidth - 1 - column) & 1u) == 0u) {
                    continue;
                }
                blit(pixels, size, glyph, heartX + column, heartY + row);
            }
        }
    }

    for (u32 letter = 0; letter < kLetters.size(); ++letter) {
        const u32 glyph = HudRenderer::kFirstLetterGlyph + letter;
        const u32 originX = (size - kLetterWidth) / 2;
        const u32 originY = (size - kLetterHeight) / 2;

        for (u32 row = 0; row < kLetterHeight; ++row) {
            const u8 bits = kLetters[letter][row];
            for (u32 column = 0; column < kLetterWidth; ++column) {
                if (((static_cast<u32>(bits) >> (kLetterWidth - 1 - column)) & 1u) == 0u) {
                    continue;
                }
                blit(pixels, size, glyph, originX + column, originY + row);
            }
        }
    }

    return pixels;
}

constexpr vec4 kSlotIdle{0.0f, 0.0f, 0.0f, 0.38f};
constexpr vec4 kSlotActive{1.0f, 1.0f, 1.0f, 0.55f};
constexpr vec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};

/// The part of a slot an item's icon fills.
///
/// The whole slot for anything that is a full cube or is not a block at all, which is
/// every item but a slab today. For a non-cube block it is the block's own bounds
/// mapped onto the slot, so a bottom slab occupies the lower half and a top slab the
/// upper -- which also distinguishes the two halves from each other, and they are two
/// different items in the pack.
vec4 iconRect(const vec4& slot, ItemId item) {
    const BlockId block = blockOfItem(item);
    if (block == kAirBlock || isFullCube(block)) {
        return slot;
    }
    const std::span<const BlockBox> boxes = blockBoxes(block);
    if (boxes.empty()) {
        return slot;
    }

    f32 lowX = boxes[0].lowX();
    f32 lowY = boxes[0].lowY();
    f32 highX = boxes[0].highX();
    f32 highY = boxes[0].highY();
    for (const BlockBox& box : boxes.subspan(1)) {
        lowX = std::min(lowX, box.lowX());
        lowY = std::min(lowY, box.lowY());
        highX = std::max(highX, box.highX());
        highY = std::max(highY, box.highY());
    }

    const f32 width = slot.z - slot.x;
    const f32 height = slot.w - slot.y;
    return vec4{slot.x + width * lowX, slot.y + height * lowY,
                slot.x + width * highX, slot.y + height * highY};
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
        push(rect, kWhite, Mode::Glyph, digit);

        x -= width;
    } while (remaining > 0);
}

void HudRenderer::pushText(std::string_view text, f32 centreX, f32 topY, f32 height,
                           f32 aspect) {
    // Advance is a little wider than the glyph so letters do not touch. The glyph
    // itself is square in the atlas, so its NDC width is the height over the aspect.
    const f32 glyphWidth = height / aspect;
    const f32 advance = glyphWidth * 0.72f;
    const f32 total = advance * static_cast<f32>(text.size());

    f32 x = centreX - total * 0.5f;
    const f32 y = topY - height;

    for (const char c : text) {
        u32 letter = 0;
        bool printable = false;
        if (c >= 'a' && c <= 'z') {
            letter = static_cast<u32>(c - 'a');
            printable = true;
        } else if (c >= 'A' && c <= 'Z') {
            letter = static_cast<u32>(c - 'A');
            printable = true;
        }

        if (printable) {
            const vec4 rect{x, y, x + glyphWidth, y + height};
            // The same hard shadow the stack counts use, and for the same reason:
            // white letters over a bright block texture are otherwise unreadable.
            const f32 shadow = height * 0.11f;
            push(vec4{rect.x + shadow / aspect, rect.y - shadow,
                      rect.z + shadow / aspect, rect.w - shadow},
                 vec4{0.0f, 0.0f, 0.0f, 0.85f}, Mode::Glyph, kFirstLetterGlyph + letter);
            push(rect, kWhite, Mode::Glyph, kFirstLetterGlyph + letter);
        }

        x += advance;
    }
}

void HudRenderer::pushSlot(const UiRect& rect, const ItemStack& stack, bool highlight,
                           f32 aspect) {
    const vec4 slot{rect.x0, rect.y0, rect.x1, rect.y1};

    // Backing plate, brighter for the selected slot. Drawn slightly larger than the
    // icon so it reads as a frame around it.
    const f32 border = (rect.x1 - rect.x0) * 0.09f;
    push(vec4{slot.x - border, slot.y - border, slot.z + border, slot.w + border},
         highlight ? kSlotActive : kSlotIdle, Mode::Solid);

    // **An empty slot is empty.** It used to draw a dimmed icon of whatever the
    // hotbar was hard-coded to hold, which meant nine blocks sat along the bottom of
    // the screen whether the player owned any of them or not. Vanilla draws nothing
    // in an empty slot, and the difference is most of what made the old HUD look
    // wrong.
    if (stack.empty()) {
        return;
    }

    // `itemIcon` rather than `kBlocks[...].top`, since Phase 16: a slot can hold a
    // stick, and a stick has no block to take a top face from.
    //
    // **A block that is not a cube draws only the part of the slot its shape fills.**
    // The icon is a flat tile of the block's texture, so a slab drawn full-size is
    // indistinguishable from the planks it was cut from -- which is not a cosmetic
    // complaint when both are in the hotbar at once and only one of them is what you
    // meant to place. Half a tile says "half a block" without a second texture.
    push(iconRect(slot, stack.item), kWhite, Mode::Block, itemIcon(stack.item));

    // A single item shows no number, exactly as in vanilla: the icon already says
    // "one", and a 1 in every slot is noise.
    if (stack.count > 1) {
        pushNumber(stack.count, slot, aspect);
    }
}

void HudRenderer::pushHearts(const ScreenLayout& layout, const State& state,
                             f32 aspect) {
    if (state.maxHealth <= 0.0f) {
        return;
    }

    const UiRect first = layout.closedHotbarSlot(0);
    const auto hearts = static_cast<u32>(state.maxHealth) / kHealthPerHeart;

    // Sized against the hotbar rather than against the screen, so they scale together
    // and sit on the same grid.
    const f32 size = (first.y1 - first.y0) * 0.42f;
    // Wider than the glyph, not narrower. At 0.92 the hearts touched and the row read
    // as one pink smear rather than as ten things you can count -- which is the whole
    // job of a heart bar.
    const f32 step = size * 1.06f;
    const f32 y = first.y1 + size * 0.35f;

    const auto health = static_cast<u32>(std::max(0.0f, state.health) + 0.5f);

    for (u32 i = 0; i < hearts; ++i) {
        const f32 x = first.x0 + static_cast<f32>(i) * step / aspect;
        const vec4 rect{x, y, x + size / aspect, y + size};

        // The dark socket first, always. Ten of them means the bar keeps its length
        // as health drops, which is what lets a player read "three left" at a glance
        // rather than counting. Nearly opaque, because at 0.55 over grass the empty
        // hearts read as shadows on the ground rather than as part of the HUD.
        push(rect, vec4{0.04f, 0.04f, 0.05f, 0.85f}, Mode::Glyph, kHeartFullGlyph);

        const u32 filled = health > i * kHealthPerHeart ? health - i * kHealthPerHeart : 0u;
        if (filled == 0) {
            continue;
        }

        const vec4 red{0.86f, 0.11f, 0.13f, 1.0f};
        push(rect, red, Mode::Glyph,
             filled >= kHealthPerHeart ? kHeartFullGlyph : kHeartHalfGlyph);
    }
}

void HudRenderer::draw(rhi::Device& device, const BlockTextures& textures,
                       const Inventory& inventory, const State& state, f32 aspect,
                       rhi::FrameRing& ring) {
    MC_PROFILE_SCOPE_N("HudRenderer::draw");

    m_quads.clear();

    const ScreenLayout layout{aspect, state.screenKind};

    if (state.screen != nullptr) {
        const Screen& screen = *state.screen;

        // A dim over the world, so the window reads as in front of it rather than
        // painted on it. Full-screen, and the first thing drawn.
        push(vec4{-1.0f, -1.0f, 1.0f, 1.0f}, vec4{0.0f, 0.0f, 0.0f, 0.55f}, Mode::Solid);

        const UiRect panel = layout.panel();
        push(vec4{panel.x0, panel.y0, panel.x1, panel.y1},
             vec4{0.11f, 0.11f, 0.13f, 0.94f}, Mode::Solid);

        // The arrow first, so the slots draw over its ends rather than under them.
        const UiRect arrow = layout.craftArrow();
        const bool furnace = state.screenKind == ScreenKind::Furnace;

        // A furnace's arrow is a progress bar: dark for the whole span, then filled
        // from the left by how far through the smelt it is. A crafting table's is
        // decoration, because a craft is instant and has nothing to show.
        push(vec4{arrow.x0, arrow.y0, arrow.x1, arrow.y1},
             furnace ? vec4{0.20f, 0.20f, 0.22f, 1.0f} : vec4{0.42f, 0.42f, 0.46f, 1.0f},
             Mode::Solid);

        if (furnace) {
            const f32 filled = std::clamp(state.cookProgress, 0.0f, 1.0f);
            if (filled > 0.0f) {
                push(vec4{arrow.x0, arrow.y0,
                          arrow.x0 + (arrow.x1 - arrow.x0) * filled, arrow.y1},
                     vec4{0.86f, 0.78f, 0.36f, 1.0f}, Mode::Solid);
            }

            // The flame, shrinking as the fuel burns down.
            //
            // **Beside the column rather than in the gap between the two slots**,
            // which is where it went first: the gap is one `kGap` tall, the gauge is
            // a third of a slot, and it was drawn before the slots -- so it ended up
            // underneath the ingredient and was invisible in the first capture. The
            // panel's left half is empty, which is where vanilla draws a player model
            // and this has nothing to put.
            const UiRect input = layout.slot(0);
            const UiRect fuel = layout.slot(1);

            const f32 width = (fuel.x1 - fuel.x0) * 0.42f;
            const f32 height = (input.y1 - fuel.y0) * 0.30f;
            const f32 right = fuel.x0 - width * 0.45f;
            const f32 base = (fuel.y0 + input.y1) * 0.5f - height * 0.5f;

            push(vec4{right - width, base, right, base + height},
                 vec4{0.16f, 0.15f, 0.14f, 0.9f}, Mode::Solid);

            const f32 fire = std::clamp(state.burnProgress, 0.0f, 1.0f);
            if (fire > 0.0f) {
                push(vec4{right - width, base, right, base + height * fire},
                     vec4{0.95f, 0.55f, 0.12f, 1.0f}, Mode::Solid);
            }
        }

        // **Every slot in the screen, drawn by one loop, including the output.** The
        // container's slots, the player's thirty-six and the crafting result are one
        // index space and one call each; a furnace will need no drawing code either.
        //
        // The output is still a preview rather than a stored stack -- `CraftingGrid`
        // computes it on demand, so there is no state to decide the fate of when the
        // grid changes underneath it.
        for (usize i = 0; i < screen.slotCount(); ++i) {
            const UiRect rect = layout.slot(i);
            const bool hovered = rect.contains(state.cursorX, state.cursorY);
            pushSlot(rect, screen.at(i), hovered, aspect);
        }

        // The dragged stack last, so it is over every slot it passes across --
        // otherwise it would disappear behind the one it is being dropped into,
        // which is exactly the moment the player is looking at it.
        if (!inventory.cursorEmpty()) {
            const UiRect reference = layout.slot(0);
            const f32 halfW = (reference.x1 - reference.x0) * 0.5f;
            const f32 halfH = (reference.y1 - reference.y0) * 0.5f;
            const UiRect held{state.cursorX - halfW, state.cursorY - halfH,
                              state.cursorX + halfW, state.cursorY + halfH};

            const vec4 slot{held.x0, held.y0, held.x1, held.y1};
            push(slot, kWhite, Mode::Block, itemIcon(inventory.cursor().item));
            if (inventory.cursor().count > 1) {
                pushNumber(inventory.cursor().count, slot, aspect);
            }
        }
    } else {
        // -----------------------------------------------------------------------
        // Crosshair. Two thin bars rather than a texture -- it is four triangles and
        // it has to be exactly centred, which arithmetic gets right and a sprite
        // does not. Hidden while the window is open, where a pointer is what aims.
        // -----------------------------------------------------------------------
        constexpr f32 kArm = 0.018f;
        constexpr f32 kThickness = 0.0022f;
        const vec4 crosshairTint{1.0f, 1.0f, 1.0f, 0.75f};

        // Centred on the aim point rather than on the screen -- see State::aimX.
        const f32 cx = state.aimX;
        const f32 cy = state.aimY;
        push(vec4{cx - kArm / aspect, cy - kThickness, cx + kArm / aspect, cy + kThickness},
             crosshairTint, Mode::Solid);
        push(vec4{cx - kThickness / aspect, cy - kArm, cx + kThickness / aspect, cy + kArm},
             crosshairTint, Mode::Solid);

        // The name of whatever is under the crosshair, just below it.
        //
        // **This is the part that still works when the picture does not.** In third
        // person the character stands between the camera and anything within arm's
        // reach, so a block being mined at the player's feet is hidden however the
        // camera is placed. Vanilla answers the same question with the F3 screen;
        // this is one line under the crosshair and always on, because "what am I
        // mining" is not a debugging question.
        if (!state.targetName.empty()) {
            constexpr f32 kNameHeight = 0.038f;
            constexpr f32 kNameGap = 0.030f;
            pushText(state.targetName, state.aimX, state.aimY - kNameGap, kNameHeight,
                     aspect);
        }

        for (usize i = 0; i < Inventory::kHotbarSlots; ++i) {
            pushSlot(layout.closedHotbarSlot(i), inventory.at(i), i == state.hotbarSlot,
                     aspect);
        }
    }

    // **Hearts are drawn in both states, which vanilla does not do.** Vanilla hides
    // them behind the inventory screen because that screen shows the player model
    // and armour in their place; this window is slots and nothing else, so the space
    // is free and health is the only status the player has. Deliberate deviation, and
    // the one place to undo it if the window ever grows the rest of that panel.
    pushHearts(layout, state, aspect);

    if (m_quads.empty()) {
        return;
    }

    const std::optional<rhi::FrameRing::Slice> slice = ring.upload(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(m_quads.data()),
                                   m_quads.size() * sizeof(GpuQuad)});
    if (!slice.has_value()) {
        return;
    }

    rhi::Buffer::barrierAfterClientWrites();

    m_shader.bind();
    textures.bind(kBlockTextureUnit);
    m_glyphs->bind(kGlyphTextureUnit);
    ring.bind(rhi::BufferTarget::Storage, kQuadBufferBinding, *slice);
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
