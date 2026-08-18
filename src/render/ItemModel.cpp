#include "render/ItemModel.hpp"

#include "core/Assert.hpp"
#include "rhi/Device.hpp"

namespace mc {
namespace {

constexpr f32 kHalfThickness = kItemThickness * 0.5f;

/// A pixel counts as part of the shape at the same threshold the fragment shaders
/// discard at. Two different cut-offs would put the rim half a texel away from the
/// silhouette the flat faces actually draw.
constexpr u8 kAlphaCutoff = 128;

bool opaqueAt(std::span<const u8> pixels, u32 size, i32 x, i32 y) {
    if (x < 0 || y < 0 || x >= static_cast<i32>(size) || y >= static_cast<i32>(size)) {
        return false;
    }
    const usize index = (static_cast<usize>(y) * size + static_cast<usize>(x)) * 4;
    return pixels[index + 3] >= kAlphaCutoff;
}

vec3 colourAt(std::span<const u8> pixels, u32 size, u32 x, u32 y) {
    const usize index = (static_cast<usize>(y) * size + x) * 4;
    // The generated tiles are sRGB, exactly as an authored PNG would be, and the
    // character shader's flat path wants linear -- see DESIGN.md 6.9, which this is
    // the fourth piece of code to have to obey.
    return vec3{rhi::srgbToLinear(static_cast<f32>(pixels[index]) / 255.0f),
                rhi::srgbToLinear(static_cast<f32>(pixels[index + 1]) / 255.0f),
                rhi::srgbToLinear(static_cast<f32>(pixels[index + 2]) / 255.0f)};
}

} // namespace

std::vector<ItemQuad> buildSpriteModel(std::span<const u8> pixels, u32 size, f32 layer) {
    MC_VERIFY(size > 0);
    MC_VERIFY(pixels.size() >= static_cast<usize>(size) * size * 4);

    std::vector<ItemQuad> quads;
    // Two flat faces and a rim that follows the silhouette. A tool's outline is a
    // long diagonal, so the perimeter is larger than a compact shape's would be.
    quads.reserve(static_cast<usize>(size) * 8);

    const f32 texel = 1.0f / static_cast<f32>(size);

    // The sprite occupies x and y in [-0.5, 0.5]. Column 0 is at the left, and **row
    // 0 is at the top**, which is the order the generators write and the order
    // `hud.vert` samples in -- getting it upside down here would hold every tool by
    // its head.
    const auto leftOf = [&](u32 x) { return static_cast<f32>(x) * texel - 0.5f; };
    const auto topOf = [&](u32 y) { return 0.5f - static_cast<f32>(y) * texel; };

    // Front, +Z. One quad for the whole tile: the alpha discard cuts the silhouette
    // out of it, so the shape costs nothing in geometry.
    quads.push_back(ItemQuad{vec3{-0.5f, -0.5f, kHalfThickness},
                             vec3{1.0f, 0.0f, 0.0f},
                             vec3{0.0f, 1.0f, 0.0f},
                             vec3{},
                             layer,
                             false});

    // Back, -Z. Wound the other way round so it faces outwards, and sampled
    // right-to-left so that column 0 still lands at the -X end of the model. See
    // `ItemQuad::mirrorU`: without that, the sprite on this face is reflected
    // relative to the rim built from the same pixels, and the two cross in an X the
    // moment the back of the tool is what the camera can see.
    quads.push_back(ItemQuad{vec3{0.5f, -0.5f, -kHalfThickness},
                             vec3{-1.0f, 0.0f, 0.0f},
                             vec3{0.0f, 1.0f, 0.0f},
                             vec3{},
                             layer,
                             true});

    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            if (!opaqueAt(pixels, size, static_cast<i32>(x), static_cast<i32>(y))) {
                continue;
            }

            const f32 xL = leftOf(x);
            const f32 xR = leftOf(x + 1);
            const f32 yT = topOf(y);
            const f32 yB = topOf(y + 1);
            const vec3 colour = colourAt(pixels, size, x, y);

            const auto rim = [&](const vec3& origin, const vec3& uAxis, const vec3& vAxis) {
                quads.push_back(ItemQuad{origin, uAxis, vAxis, colour, ItemQuad::kFlatColour});
            };

            // A face wherever the neighbour is not part of the shape. Interior edges
            // produce nothing, which is why a solid icon costs a rim only around its
            // outline rather than one per pixel.
            if (!opaqueAt(pixels, size, static_cast<i32>(x) - 1, static_cast<i32>(y))) {
                rim(vec3{xL, yB, kHalfThickness}, vec3{0.0f, yT - yB, 0.0f},
                    vec3{0.0f, 0.0f, -kItemThickness});
            }
            if (!opaqueAt(pixels, size, static_cast<i32>(x) + 1, static_cast<i32>(y))) {
                rim(vec3{xR, yB, -kHalfThickness}, vec3{0.0f, yT - yB, 0.0f},
                    vec3{0.0f, 0.0f, kItemThickness});
            }
            if (!opaqueAt(pixels, size, static_cast<i32>(x), static_cast<i32>(y) - 1)) {
                rim(vec3{xL, yT, kHalfThickness}, vec3{xR - xL, 0.0f, 0.0f},
                    vec3{0.0f, 0.0f, -kItemThickness});
            }
            if (!opaqueAt(pixels, size, static_cast<i32>(x), static_cast<i32>(y) + 1)) {
                rim(vec3{xL, yB, -kHalfThickness}, vec3{xR - xL, 0.0f, 0.0f},
                    vec3{0.0f, 0.0f, kItemThickness});
            }
        }
    }

    return quads;
}

std::vector<ItemQuad> buildBlockModel(f32 topLayer, f32 sideLayer, f32 bottomLayer) {
    constexpr f32 h = 0.5f;

    // Face order, origins and axes are `CharacterRenderer::appendBox`'s, for the same
    // reason it has them: cross(U, V) has to point out of the cube or back-face
    // culling removes the faces that should be visible and keeps the ones that
    // should not.
    return {
        // +X
        ItemQuad{vec3{h, -h, -h}, vec3{0.0f, 1.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f},
                 vec3{}, sideLayer},
        // -X
        ItemQuad{vec3{-h, -h, -h}, vec3{0.0f, 0.0f, 1.0f}, vec3{0.0f, 1.0f, 0.0f},
                 vec3{}, sideLayer},
        // +Y
        ItemQuad{vec3{-h, h, -h}, vec3{0.0f, 0.0f, 1.0f}, vec3{1.0f, 0.0f, 0.0f},
                 vec3{}, topLayer},
        // -Y
        ItemQuad{vec3{-h, -h, -h}, vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, 1.0f},
                 vec3{}, bottomLayer},
        // +Z
        ItemQuad{vec3{-h, -h, h}, vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f},
                 vec3{}, sideLayer},
        // -Z
        ItemQuad{vec3{-h, -h, -h}, vec3{0.0f, 1.0f, 0.0f}, vec3{1.0f, 0.0f, 0.0f},
                 vec3{}, sideLayer},
    };
}

} // namespace mc
