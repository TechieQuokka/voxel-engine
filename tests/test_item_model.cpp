#include <doctest/doctest.h>

#include "render/ItemModel.hpp"

#include <array>
#include <vector>

using namespace mc;

// The held-item model is geometry built from pixels, which makes it the one part of
// drawing a tool in the hand that a test can reach: no context, no device, no window.
// What cannot be checked here is whether the result *looks* like Minecraft, and that
// is what `--hold <item> --capture` is for.

namespace {

constexpr u32 kSize = 16;
constexpr f32 kLayer = 7.0f;

/// A blank sprite, fully transparent.
std::vector<u8> emptySprite() {
    return std::vector<u8>(static_cast<usize>(kSize) * kSize * 4, 0);
}

void setPixel(std::vector<u8>& sprite, u32 x, u32 y, u8 r, u8 g, u8 b, u8 a = 255) {
    const usize index = (static_cast<usize>(y) * kSize + x) * 4;
    sprite[index] = r;
    sprite[index + 1] = g;
    sprite[index + 2] = b;
    sprite[index + 3] = a;
}

vec3 normalOf(const ItemQuad& quad) {
    return math::normalize(math::cross(quad.uAxis, quad.vAxis));
}

usize rimCount(const std::vector<ItemQuad>& model) {
    usize count = 0;
    for (const ItemQuad& quad : model) {
        if (quad.layer == ItemQuad::kFlatColour) {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("an empty sprite is still two faces and no rim") {
    // The flat faces are one quad each whatever the shape: the alpha discard in the
    // shader is what cuts the silhouette, so an empty icon draws nothing without the
    // geometry knowing anything about it.
    const std::vector<ItemQuad> model = buildSpriteModel(emptySprite(), kSize, kLayer);

    CHECK(model.size() == 2);
    CHECK(rimCount(model) == 0);
    CHECK(model[0].layer == kLayer);
    CHECK(model[1].layer == kLayer);
}

TEST_CASE("a lone pixel gets a rim on all four sides") {
    std::vector<u8> sprite = emptySprite();
    setPixel(sprite, 8, 8, 200, 100, 50);

    const std::vector<ItemQuad> model = buildSpriteModel(sprite, kSize, kLayer);

    CHECK(model.size() == 6); // two faces and four edges
    CHECK(rimCount(model) == 4);

    // The four rim normals are the four directions in the sprite's plane, each once.
    std::array<bool, 4> seen{};
    for (const ItemQuad& quad : model) {
        if (quad.layer != ItemQuad::kFlatColour) {
            continue;
        }
        const vec3 n = normalOf(quad);
        if (n.x < -0.9f) { seen[0] = true; }
        if (n.x > 0.9f) { seen[1] = true; }
        if (n.y < -0.9f) { seen[2] = true; }
        if (n.y > 0.9f) { seen[3] = true; }
    }
    CHECK(seen[0]);
    CHECK(seen[1]);
    CHECK(seen[2]);
    CHECK(seen[3]);
}

TEST_CASE("a solid sprite has a rim only around its outline") {
    // **The interior is where a naive extrusion goes wrong**: a face per pixel edge
    // would be 16x16x4 quads, almost all of them buried inside the model where no
    // camera can ever see them.
    std::vector<u8> sprite = emptySprite();
    for (u32 y = 0; y < kSize; ++y) {
        for (u32 x = 0; x < kSize; ++x) {
            setPixel(sprite, x, y, 128, 128, 128);
        }
    }

    const std::vector<ItemQuad> model = buildSpriteModel(sprite, kSize, kLayer);

    CHECK(rimCount(model) == 4 * kSize); // the perimeter, and not one face more
    CHECK(model.size() == 4 * kSize + 2);
}

TEST_CASE("a hole inside the shape is rimmed too") {
    // Not an academic case: a tool's head is drawn over its shaft and the two do not
    // always meet, and vanilla's own icons have enclosed gaps. The inside of a hole
    // is as visible as the outside.
    std::vector<u8> sprite = emptySprite();
    for (u32 y = 4; y < 8; ++y) {
        for (u32 x = 4; x < 8; ++x) {
            setPixel(sprite, x, y, 90, 90, 90);
        }
    }
    setPixel(sprite, 5, 5, 0, 0, 0, 0); // punch one pixel out of the middle

    const std::vector<ItemQuad> model = buildSpriteModel(sprite, kSize, kLayer);

    // 4x4 outline is 16 edges; the hole adds four more.
    CHECK(rimCount(model) == 16 + 4);
}

TEST_CASE("the model is one block across and one pixel thick") {
    std::vector<u8> sprite = emptySprite();
    for (u32 i = 0; i < kSize; ++i) {
        setPixel(sprite, i, i, 10, 20, 30);
    }

    const std::vector<ItemQuad> model = buildSpriteModel(sprite, kSize, kLayer);

    for (const ItemQuad& quad : model) {
        for (const vec3& corner : {quad.origin, quad.origin + quad.uAxis,
                                   quad.origin + quad.vAxis,
                                   quad.origin + quad.uAxis + quad.vAxis}) {
            CHECK(corner.x >= -0.5f - 1e-5f);
            CHECK(corner.x <= 0.5f + 1e-5f);
            CHECK(corner.y >= -0.5f - 1e-5f);
            CHECK(corner.y <= 0.5f + 1e-5f);
            CHECK(std::abs(corner.z) <= kItemThickness * 0.5f + 1e-5f);
        }
    }
}

TEST_CASE("row zero is the top of the model") {
    // **Textures are top-row-first here**, so a sprite's first row has to end up at
    // +Y or every tool is held by its head. `hud.vert` makes the same flip, and this
    // is the geometry that has to agree with it.
    std::vector<u8> sprite = emptySprite();
    setPixel(sprite, 8, 0, 255, 0, 0);

    const std::vector<ItemQuad> model = buildSpriteModel(sprite, kSize, kLayer);

    for (const ItemQuad& quad : model) {
        if (quad.layer == ItemQuad::kFlatColour) {
            CHECK(quad.origin.y > 0.4f);
        }
    }
}

TEST_CASE("only the back face samples the sprite backwards") {
    // **This is a bug that shipped in a capture before it was understood.** A quad's
    // texture coordinate comes from its corner, so the back face -- wound the other
    // way so it points outwards -- samples column 0 at the +X end of the model. The
    // image on it is then a reflection of the rim built from the very same pixels,
    // and the moment the back of the tool is what the camera sees, the drawn shape
    // and its edges cross in an X. It looked like two objects.
    const std::vector<ItemQuad> model = buildSpriteModel(emptySprite(), kSize, kLayer);

    CHECK_FALSE(model[0].mirrorU); // front
    CHECK(model[1].mirrorU);       // back

    // And no rim face asks for it: a rim carries a colour, and mirroring a colour is
    // not a thing.
    for (const ItemQuad& quad : buildSpriteModel(emptySprite(), kSize, kLayer)) {
        if (quad.layer == ItemQuad::kFlatColour) {
            CHECK_FALSE(quad.mirrorU);
        }
    }
}

TEST_CASE("the two flat faces point in opposite directions") {
    const std::vector<ItemQuad> model = buildSpriteModel(emptySprite(), kSize, kLayer);

    const vec3 front = normalOf(model[0]);
    const vec3 back = normalOf(model[1]);

    CHECK(front.z > 0.9f);
    CHECK(back.z < -0.9f);
}

TEST_CASE("a rim face takes the colour of the pixel it belongs to, in linear light") {
    // Every other renderer in this engine has had to learn this one -- see
    // DESIGN.md 6.9. A rim quad carries a colour rather than a texture, so nothing
    // decodes it later and it has to arrive linear.
    std::vector<u8> sprite = emptySprite();
    setPixel(sprite, 8, 8, 255, 0, 0);

    const std::vector<ItemQuad> model = buildSpriteModel(sprite, kSize, kLayer);

    for (const ItemQuad& quad : model) {
        if (quad.layer != ItemQuad::kFlatColour) {
            continue;
        }
        CHECK(quad.color.x == doctest::Approx(1.0f));
        CHECK(quad.color.y == doctest::Approx(0.0f));
        CHECK(quad.color.z == doctest::Approx(0.0f));
    }
}

TEST_CASE("a half-transparent pixel is out, at the same cut-off the shader uses") {
    // Two different thresholds would put the rim half a texel away from the
    // silhouette the flat faces actually draw, which reads as a fringe.
    std::vector<u8> sprite = emptySprite();
    setPixel(sprite, 8, 8, 255, 255, 255, 127);

    CHECK(rimCount(buildSpriteModel(sprite, kSize, kLayer)) == 0);

    setPixel(sprite, 8, 8, 255, 255, 255, 128);
    CHECK(rimCount(buildSpriteModel(sprite, kSize, kLayer)) == 4);
}

TEST_CASE("a held block is six outward faces with the block's own layers") {
    const std::vector<ItemQuad> model = buildBlockModel(3.0f, 4.0f, 5.0f);

    REQUIRE(model.size() == 6);

    usize top = 0;
    usize bottom = 0;
    usize side = 0;
    for (const ItemQuad& quad : model) {
        const vec3 n = normalOf(quad);
        // Every face must point out of the cube, or back-face culling keeps exactly
        // the wrong half of it.
        const vec3 centre = quad.origin + (quad.uAxis + quad.vAxis) * 0.5f;
        CHECK(math::dot(n, centre) > 0.0f);

        if (n.y > 0.9f) {
            ++top;
            CHECK(quad.layer == 3.0f);
        } else if (n.y < -0.9f) {
            ++bottom;
            CHECK(quad.layer == 5.0f);
        } else {
            ++side;
            CHECK(quad.layer == 4.0f);
        }
    }

    CHECK(top == 1);
    CHECK(bottom == 1);
    CHECK(side == 4);
}
