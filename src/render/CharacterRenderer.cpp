#include "render/CharacterRenderer.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "world/BlockShape.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace mc {
namespace {

/// Model units. Minecraft draws the player at 16 units to the block, and keeping
/// that ratio is what makes the proportions read as the thing they are copying:
/// an 8x8x8 head, an 8x12x4 body, 4x12x4 limbs.
constexpr f32 kUnit = 1.0f / 16.0f;
constexpr f32 u(f32 units) { return units * kUnit; }

/// sRGB, decoded once when a quad is built. Steve's palette.
constexpr u32 kSkin = 0xC69076u;
constexpr u32 kHair = 0x33241Au;
constexpr u32 kShirt = 0x00A8A8u;
constexpr u32 kPants = 0x3A3A9Eu;

vec4 linearOf(u32 rgb) {
    return vec4{rhi::srgbToLinear(static_cast<f32>((rgb >> 16) & 0xFFu) / 255.0f),
                rhi::srgbToLinear(static_cast<f32>((rgb >> 8) & 0xFFu) / 255.0f),
                rhi::srgbToLinear(static_cast<f32>(rgb & 0xFFu) / 255.0f),
                1.0f};
}

/// Rotation about the X axis, which is the only one a limb needs: shoulders and
/// hips swing the leg forward and back and nothing else.
vec3 swingPoint(const vec3& point, const vec3& pivot, f32 cosA, f32 sinA) {
    const f32 dy = point.y - pivot.y;
    const f32 dz = point.z - pivot.z;
    return vec3{point.x,
                pivot.y + dy * cosA - dz * sinA,
                pivot.z + dy * sinA + dz * cosA};
}

vec3 swingVector(const vec3& v, f32 cosA, f32 sinA) {
    return vec3{v.x, v.y * cosA - v.z * sinA, v.y * sinA + v.z * cosA};
}

/// The three axis rotations, spelled out rather than reached for in a glm extension.
///
/// Minecraft composes a display rotation as X, then Y, then Z, and the order is not
/// a detail: -90 about Y turns a tool's flat face to the side, and 55 about Z tilts
/// it up out of the fist. Swapping them tilts first and turns the tilt into a lean.
mat3 rotationX(f32 radians) {
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);
    return mat3{vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, c, s}, vec3{0.0f, -s, c}};
}

mat3 rotationY(f32 radians) {
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);
    return mat3{vec3{c, 0.0f, -s}, vec3{0.0f, 1.0f, 0.0f}, vec3{s, 0.0f, c}};
}

mat3 rotationZ(f32 radians) {
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);
    return mat3{vec3{c, s, 0.0f}, vec3{-s, c, 0.0f}, vec3{0.0f, 0.0f, 1.0f}};
}

/// Shoulder angle of the mining chop, in radians.
///
/// Negative swings the arm forward -- see swingPoint: a point below the pivot moves
/// toward -Z as the angle rises, and the model faces +Z. The arc runs from about
/// -109 degrees (raised, arm nearly overhead-forward) to -46 (chopped down), which
/// is the shape of Minecraft's swing rather than a full windmill.
f32 miningAngle(f32 phase) {
    constexpr f32 kCentre = -1.35f;
    constexpr f32 kReach = 0.55f;
    return kCentre + kReach * std::cos(phase);
}

} // namespace

CharacterRenderer::CharacterRenderer() {
    m_shader = rhi::Shader::fromFiles(assetPath("shaders/character.vert"),
                                      assetPath("shaders/character.frag"));
    m_shader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));

    // Feet at y = 0, facing +Z. Legs 12 units, body 12, head 8: two blocks tall,
    // which is the model rather than the 1.8-block hitbox.
    const vec3 shoulderR{-u(6.0f), u(24.0f), 0.0f};
    const vec3 shoulderL{u(6.0f), u(24.0f), 0.0f};
    const vec3 hipR{-u(2.0f), u(12.0f), 0.0f};
    const vec3 hipL{u(2.0f), u(12.0f), 0.0f};

    m_boxes = {{
        // head: 8x8x8, sitting on the body
        {{-u(4), u(24), -u(4)}, {u(4), u(32), u(4)}, {}, 0.0f, false, kSkin, kHair},
        // Hair, as a shell over the top of the head rather than a colour on its
        // top face: from behind, a face-coloured head reads as a bald mannequin.
        // Inflated by a tenth of a unit so it cannot z-fight with the head.
        {{-u(4.1f), u(29), -u(4.1f)}, {u(4.1f), u(32.1f), u(4.1f)}, {}, 0.0f, false,
         kHair, kHair},
        // body: 8x12x4
        {{-u(4), u(12), -u(2)}, {u(4), u(24), u(2)}, {}, 0.0f, false, kShirt, kShirt},

        // Arms split at the sleeve. Two boxes rather than one is most of what
        // makes the silhouette read as a person rather than a stack of crates.
        // The right arm is flagged: it is the one that mines.
        {{-u(8), u(19), -u(2)}, {-u(4), u(24), u(2)}, shoulderR, 1.0f, true, kShirt, kShirt},
        {{-u(8), u(12), -u(2)}, {-u(4), u(19), u(2)}, shoulderR, 1.0f, true, kSkin, kSkin},
        {{u(4), u(19), -u(2)}, {u(8), u(24), u(2)}, shoulderL, -1.0f, false, kShirt, kShirt},
        {{u(4), u(12), -u(2)}, {u(8), u(19), u(2)}, shoulderL, -1.0f, false, kSkin, kSkin},

        // Legs swing against the arm on the same side, which is what walking is.
        {{-u(4), 0.0f, -u(2)}, {0.0f, u(12), u(2)}, hipR, -1.0f, false, kPants, kPants},
        {{0.0f, 0.0f, -u(2)}, {u(4), u(12), u(2)}, hipL, 1.0f, false, kPants, kPants},
    }};

    m_quads.reserve(kQuadCount);
}

void CharacterRenderer::appendBox(const Box& box, f32 swingAngle, const vec3& feetPosition,
                                  const vec3& right, const vec3& up, const vec3& forward) {
    const f32 cosA = std::cos(swingAngle);
    const f32 sinA = std::sin(swingAngle);
    const bool swings = box.swing != 0.0f;

    const vec3 size = box.max - box.min;

    // Origin, U and V per face, chosen so cross(U, V) points out of the box. Same
    // winding rule the chunk mesher follows, so one culling state serves both.
    const std::array<vec3, kQuadsPerBox> origins{{
        {box.max.x, box.min.y, box.min.z}, // +X
        {box.min.x, box.min.y, box.min.z}, // -X
        {box.min.x, box.max.y, box.min.z}, // +Y
        {box.min.x, box.min.y, box.min.z}, // -Y
        {box.min.x, box.min.y, box.max.z}, // +Z
        {box.min.x, box.min.y, box.min.z}, // -Z
    }};
    const std::array<vec3, kQuadsPerBox> uAxes{{
        {0.0f, size.y, 0.0f}, {0.0f, 0.0f, size.z},
        {0.0f, 0.0f, size.z}, {size.x, 0.0f, 0.0f},
        {size.x, 0.0f, 0.0f}, {0.0f, size.y, 0.0f},
    }};
    const std::array<vec3, kQuadsPerBox> vAxes{{
        {0.0f, 0.0f, size.z}, {0.0f, size.y, 0.0f},
        {size.x, 0.0f, 0.0f}, {0.0f, 0.0f, size.z},
        {0.0f, size.y, 0.0f}, {size.x, 0.0f, 0.0f},
    }};

    const vec4 side = linearOf(box.sideArgb);
    const vec4 top = linearOf(box.topArgb);

    const auto toWorld = [&](const vec3& p) {
        return feetPosition + right * p.x + up * p.y + forward * p.z;
    };
    const auto toWorldVector = [&](const vec3& v) {
        return right * v.x + up * v.y + forward * v.z;
    };

    for (usize face = 0; face < kQuadsPerBox; ++face) {
        vec3 origin = origins[face];
        vec3 uAxis = uAxes[face];
        vec3 vAxis = vAxes[face];

        if (swings) {
            origin = swingPoint(origin, box.pivot, cosA, sinA);
            uAxis = swingVector(uAxis, cosA, sinA);
            vAxis = swingVector(vAxis, cosA, sinA);
        }

        const vec3 worldOrigin = toWorld(origin);
        const vec3 worldU = toWorldVector(uAxis);
        const vec3 worldV = toWorldVector(vAxis);

        m_quads.push_back(GpuQuad{
            // **`w` is the texture layer now, and the player model has none.**
            // Leaving the old 0.0f here would have drawn every box as texture layer
            // zero, which is stone -- a player made of rock, and a silent one,
            // because 0 is a perfectly valid layer.
            vec4{worldOrigin.x, worldOrigin.y, worldOrigin.z, ItemQuad::kFlatColour},
            vec4{worldU.x, worldU.y, worldU.z, 0.0f},
            vec4{worldV.x, worldV.y, worldV.z, 0.0f},
            face == 2 ? top : side,
        });
    }
}

const std::vector<ItemQuad>& CharacterRenderer::modelFor(ItemId item,
                                                        const BlockTextures& textures) {
    const auto found = m_itemModels.find(item);
    if (found != m_itemModels.end()) {
        return found->second;
    }

    // **A block is held as a block and an item as a picture given depth.** That is
    // vanilla's split and it is not cosmetic: a cobblestone held as a flat sprite of
    // its top face reads as a tile, and a pickaxe held as a cube reads as a crate.
    std::vector<ItemQuad> model;
    if (itemIsBlock(item)) {
        const BlockId block = blockOfItem(item);
        const BlockInfo& info = kBlocks[block];

        // The union of the block's boxes, which is the box itself for a slab and the
        // whole cube for everything else. A stair will want the union of two and this
        // already gives it.
        vec3 low{0.0f};
        vec3 high{1.0f};
        const std::span<const BlockBox> boxes = blockBoxes(block);
        if (!boxes.empty()) {
            low = vec3{boxes[0].lowX(), boxes[0].lowY(), boxes[0].lowZ()};
            high = vec3{boxes[0].highX(), boxes[0].highY(), boxes[0].highZ()};
            for (const BlockBox& box : boxes.subspan(1)) {
                low = math::min(low, vec3{box.lowX(), box.lowY(), box.lowZ()});
                high = math::max(high, vec3{box.highX(), box.highY(), box.highZ()});
            }
        }

        model = buildBlockModel(static_cast<f32>(info.top), static_cast<f32>(info.side),
                                static_cast<f32>(info.bottom), low, high);
    } else {
        const u16 layer = itemIcon(item);

        model = buildSpriteModel(textures.layerPixels(layer), BlockTextures::kTextureSize,
                                 static_cast<f32>(layer));
    }

    return m_itemModels.emplace(item, std::move(model)).first->second;
}

void CharacterRenderer::appendHeldItem(const std::vector<ItemQuad>& model,
                                       const HeldTransform& display, const vec3& anchor,
                                       const vec3& right, const vec3& up,
                                       const vec3& forward) {
    // Scale, then rotate, then translate -- the order Minecraft applies a display
    // transform in. Doing the translation first would move the item along the
    // *rotated* axes and swing it out of the hand as the tilt changed.
    const mat3 rotation = rotationX(math::radians(display.rotationDegrees.x))
                        * rotationY(math::radians(display.rotationDegrees.y))
                        * rotationZ(math::radians(display.rotationDegrees.z));

    // **Minecraft's model space is Y-down and Z-back; this engine's is Y-up and
    // Z-forward.** The two differ by a half turn about X, so the display numbers
    // above are conjugated into this engine's frame rather than used as they stand:
    // a rotation becomes `C R C^-1` and a translation becomes `C T`.
    //
    // **The model itself is not turned, and that is the whole subtlety.** Turning
    // the frame *and* the model leaves a tool hanging head-down, which is what the
    // first attempt at this drew: the conversion belongs to the transform, not to
    // the sprite, whose own +Y is up in both worlds.
    const mat3 halfTurnX{vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, -1.0f, 0.0f},
                         vec3{0.0f, 0.0f, -1.0f}};

    const mat3 inFrame = halfTurnX * rotation * halfTurnX;
    const vec3 translation = halfTurnX * (display.translationTexels * kUnit);

    // Into the hand's frame, and then into the world. The frame is the character's
    // basis rotated by the arm's swing in third person and the camera's in first,
    // which is the whole reason this takes a basis rather than a matrix.
    const auto toWorld = [&](const vec3& p) {
        const vec3 inHand = translation + inFrame * (p * display.scale);
        return anchor + right * inHand.x + up * inHand.y + forward * inHand.z;
    };
    const auto toWorldVector = [&](const vec3& v) {
        const vec3 inHand = inFrame * (v * display.scale);
        return right * inHand.x + up * inHand.y + forward * inHand.z;
    };

    for (const ItemQuad& quad : model) {
        const vec3 origin = toWorld(quad.origin);
        const vec3 uAxis = toWorldVector(quad.uAxis);
        const vec3 vAxis = toWorldVector(quad.vAxis);

        m_quads.push_back(GpuQuad{
            vec4{origin.x, origin.y, origin.z, quad.layer},
            vec4{uAxis.x, uAxis.y, uAxis.z, quad.mirrorU ? 1.0f : 0.0f},
            vec4{vAxis.x, vAxis.y, vAxis.z, 0.0f},
            vec4{quad.color.x, quad.color.y, quad.color.z, 1.0f},
        });
    }
}

void CharacterRenderer::draw(rhi::Device& device, const Camera& camera,
                             const vec3& feetPosition, const vec3& facing,
                             f32 walkPhase, f32 walkAmount,
                             f32 swingPhase, f32 swingAmount, ItemId heldItem,
                             const BlockTextures& textures, rhi::FrameRing& ring) {
    MC_PROFILE_SCOPE_N("CharacterRenderer::draw");

    // A basis from whatever the caller calls forward, so the model needs no
    // agreement with the camera about which way yaw counts.
    vec3 forward = facing;
    forward.y = 0.0f;
    if (math::dot(forward, forward) < 1e-6f) {
        forward = vec3{0.0f, 0.0f, 1.0f};
    }
    forward = math::normalize(forward);

    const vec3 up = Camera::up();
    const vec3 right = math::normalize(math::cross(up, forward));

    // Peak swing of about 35 degrees, faded by how fast the character is moving.
    constexpr f32 kMaxSwing = 0.62f;
    const f32 amplitude = kMaxSwing * math::clamp(walkAmount, 0.0f, 1.0f);

    const f32 chop = math::clamp(swingAmount, 0.0f, 1.0f);

    m_quads.clear();
    for (const Box& box : m_boxes) {
        const f32 walkAngle = box.swing * amplitude * std::sin(walkPhase);

        // The right arm blends from its walk swing into the chop rather than
        // snapping, so starting and stopping a dig does not pop the limb. Every
        // other box keeps walking -- mining while moving is both at once.
        const f32 angle = box.rightArm
                              ? math::mix(walkAngle, miningAngle(swingPhase), chop)
                              : walkAngle;

        appendBox(box, angle, feetPosition, right, up, forward);
    }

    if (itemExists(heldItem)) {
        // The angle the right arm ended up at, recomputed rather than remembered --
        // the same expression the loop above uses for the arm that holds the tool.
        const f32 walkAngle = amplitude * std::sin(walkPhase);
        const f32 armAngle = math::mix(walkAngle, miningAngle(swingPhase), chop);
        const f32 cosA = std::cos(armAngle);
        const f32 sinA = std::sin(armAngle);

        // The fist, at the far end of the lower right arm and swung with it. The
        // model's right arm hangs from x = -6 units, and the arm box stops at 12 up,
        // which is where a hand is.
        const vec3 shoulderRight{-u(6.0f), u(24.0f), 0.0f};
        const vec3 fistRest{-u(6.0f), u(12.0f), 0.0f};

        // **The hand's frame is the model's frame, swung.** Passing the character's
        // basis unrotated would leave the tool pointing forward while the arm went
        // up, which is a tool sliding through a fist rather than one held by it.
        const vec3 fist = swingPoint(fistRest, shoulderRight, cosA, sinA);
        const vec3 handRight = swingVector(vec3{1.0f, 0.0f, 0.0f}, cosA, sinA);
        const vec3 handUp = swingVector(vec3{0.0f, 1.0f, 0.0f}, cosA, sinA);
        const vec3 handForward = swingVector(vec3{0.0f, 0.0f, 1.0f}, cosA, sinA);

        const auto intoWorld = [&](const vec3& p) {
            return feetPosition + right * p.x + up * p.y + forward * p.z;
        };
        const auto intoWorldVector = [&](const vec3& v) {
            return right * v.x + up * v.y + forward * v.z;
        };

        appendHeldItem(modelFor(heldItem, textures),
                       itemIsBlock(heldItem) ? kBlockThirdPerson : kHandheldThirdPerson,
                       intoWorld(fist), intoWorldVector(handRight),
                       intoWorldVector(handUp), intoWorldVector(handForward));
    }

    const std::optional<rhi::FrameRing::Slice> slice = ring.upload(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(m_quads.data()),
                                   m_quads.size() * sizeof(GpuQuad)});
    if (!slice.has_value()) {
        return;
    }

    rhi::Buffer::barrierAfterClientWrites();

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());

    // Bound whether or not anything is held: the shader samples only where a quad
    // names a layer, but leaving the unit unbound is undefined rather than merely
    // unread.
    textures.bind(kTextureUnit);
    ring.bind(rhi::BufferTarget::Storage, kQuadBufferBinding, *slice);
    m_vao.bind();

    device.drawTriangles(static_cast<u32>(m_quads.size()) * kVerticesPerQuad);
}

void CharacterRenderer::drawHand(rhi::Device& device, const Camera& camera,
                                 f32 swingPhase, f32 swingAmount, ItemId heldItem,
                                 const BlockTextures& textures, rhi::FrameRing& ring) {
    MC_PROFILE_SCOPE_N("CharacterRenderer::drawHand");

    const f32 chop = math::clamp(swingAmount, 0.0f, 1.0f);

    // Where the arm sits relative to the eye, in blocks: right, down, and forward.
    // Picked to sit in the lower-right corner at a 70-degree vertical FOV without
    // covering what the crosshair is on.
    constexpr f32 kRight = 0.38f;
    constexpr f32 kDown = 0.45f;
    constexpr f32 kForward = 0.66f;

    // The swing moves the whole arm rather than rotating a shoulder that is off
    // screen: in first person there is no visible joint for a rotation to read
    // against, so translation is what the eye actually sees. Minecraft does the
    // same thing -- its first-person swing is mostly a position curve.
    const f32 lift = std::sin(swingPhase) * 0.10f * chop;
    const f32 punch = (std::cos(swingPhase) * 0.5f + 0.5f) * 0.16f * chop;

    const vec3 right = camera.right();
    const vec3 up = Camera::up();
    const vec3 forward = camera.forward();

    const vec3 anchor = camera.position()
                      + right * kRight
                      + up * (-kDown + lift)
                      + forward * (kForward + punch);

    // One box, in the same model units as the third-person arm: 4x4x12, pointing
    // away from the viewer and tilted so it reads as an arm rather than a plank.
    const vec3 half{u(2.0f), u(2.0f), u(6.0f)};
    const vec3 tilt = math::normalize(forward + up * 0.55f - right * 0.18f);
    const vec3 across = math::normalize(math::cross(up, tilt));
    const vec3 vertical = math::cross(tilt, across);

    const Box arm{-half, half, {}, 0.0f, false, kSkin, kSkin};

    m_quads.clear();
    appendBox(arm, 0.0f, anchor, across, vertical, tilt);

    if (itemExists(heldItem)) {
        // **The item's axes are given here rather than through vanilla's first-person
        // rotation, and that is a deliberate deviation.** Vanilla's `[0, -90, 25]` is
        // expressed in the frame of a rigged arm with a wrist; this engine's view
        // model is a single tilted box and has no such frame, so those numbers land
        // somewhere arbitrary in it -- three captures' worth of somewhere arbitrary.
        // What is copied is the *look* they produce, stated directly:
        //
        //   - the flat face turned towards the eye, tipped just enough that the
        //     extrusion's edge shows and the tool reads as an object,
        //   - the sprite's up -- handle at the bottom, head at the top -- pointing up
        //     and to the left, so the head sits towards the crosshair,
        //   - the handle running down into the fist.
        //
        // Vanilla's scale is kept, because a scale needs no frame to mean something.
        const vec3 itemUp = math::normalize(up * 0.86f - right * 0.51f);
        const vec3 towardsEye = math::normalize(-forward * 0.98f + right * 0.12f);
        const vec3 planeRight = math::normalize(math::cross(itemUp, towardsEye));

        // **Turned to show its other side, which is a rotation and not a mirror.**
        // The icon is drawn with its handle running to the lower left, and the hand
        // is at the lower *right*. Half a turn about the item's own up axis puts the
        // handle where the fist is and leaves the head where it belongs, up and
        // towards the crosshair with its horns still pointing down. Vanilla mirrors
        // its model for the same reason; a mirror here would reverse every winding
        // and back-face culling would then keep the wrong half of the tool.
        //
        // **Rolling the sprite in its plane was tried first and is wrong**: it puts
        // the handle in the right place by turning the crescent on its side, which
        // reads as a hook rather than as a pickaxe. One capture said so.
        const vec3 itemRight = -planeRight;
        const vec3 itemNormal = math::cross(itemRight, itemUp);

        // **Held further down the arm than the fist, because it is too close to the
        // eye otherwise.** At 0.7 blocks from a 70-degree camera a 0.68-block tool
        // fills a third of the screen and crosses the crosshair; vanilla's sits in
        // the lower right and leaves the aim clear. Pushing it along the arm is what
        // buys that back without shrinking the model away from vanilla's scale.
        const vec3 fist = anchor + tilt * u(9.0f);

        // Offset up and left of the fist, so what disappears into the hand is the
        // *handle* rather than the middle of the tool. A held block has no handle and
        // sits in the hand itself -- offsetting a cube would hold it in mid-air.
        const vec3 centre =
            itemIsBlock(heldItem) ? fist : fist + itemUp * 0.11f + right * 0.04f;

        appendHeldItem(modelFor(heldItem, textures),
                       itemIsBlock(heldItem) ? kBlockFirstPerson : kHandheldFirstPerson,
                       centre, itemRight, itemUp, itemNormal);
    }

    const std::optional<rhi::FrameRing::Slice> slice = ring.upload(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(m_quads.data()),
                                   m_quads.size() * sizeof(GpuQuad)});
    if (!slice.has_value()) {
        return;
    }

    rhi::Buffer::barrierAfterClientWrites();

    // **Depth cleared first.** The arm lives half a block from the eye, so standing
    // against a wall would otherwise bury it inside the terrain. Clearing means it
    // always draws on top, which is what a view model is for and what Minecraft
    // does. Everything that needs to depth-test against the world -- terrain, the
    // character, the selection box, the cracks -- has already been drawn.
    device.clearDepth();

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());

    // Bound whether or not anything is held: the shader samples only where a quad
    // names a layer, but leaving the unit unbound is undefined rather than merely
    // unread.
    textures.bind(kTextureUnit);
    ring.bind(rhi::BufferTarget::Storage, kQuadBufferBinding, *slice);
    m_vao.bind();

    device.drawTriangles(static_cast<u32>(m_quads.size()) * kVerticesPerQuad);
}

} // namespace mc
