#include "render/CharacterRenderer.hpp"

#include "core/Paths.hpp"
#include "core/Profile.hpp"

#include <cmath>
#include <cstddef>
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
    m_buffer = rhi::Buffer::createPersistent(kQuadCount * sizeof(GpuQuad));
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
            vec4{worldOrigin.x, worldOrigin.y, worldOrigin.z, 0.0f},
            vec4{worldU.x, worldU.y, worldU.z, 0.0f},
            vec4{worldV.x, worldV.y, worldV.z, 0.0f},
            face == 2 ? top : side,
        });
    }
}

void CharacterRenderer::draw(rhi::Device& device, const Camera& camera,
                             const vec3& feetPosition, const vec3& facing,
                             f32 walkPhase, f32 walkAmount,
                             f32 swingPhase, f32 swingAmount) {
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

    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(m_quads.data()), m_quads.size() * sizeof(GpuQuad)};
    m_buffer->write(0, bytes);
    rhi::Buffer::barrierAfterClientWrites();

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());

    m_buffer->bindBase(rhi::BufferTarget::Storage, kQuadBufferBinding);
    m_vao.bind();

    device.drawTriangles(static_cast<u32>(m_quads.size()) * kVerticesPerQuad);
}

void CharacterRenderer::drawHand(rhi::Device& device, const Camera& camera,
                                 f32 swingPhase, f32 swingAmount) {
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

    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(m_quads.data()), m_quads.size() * sizeof(GpuQuad)};
    m_buffer->write(0, bytes);
    rhi::Buffer::barrierAfterClientWrites();

    // **Depth cleared first.** The arm lives half a block from the eye, so standing
    // against a wall would otherwise bury it inside the terrain. Clearing means it
    // always draws on top, which is what a view model is for and what Minecraft
    // does. Everything that needs to depth-test against the world -- terrain, the
    // character, the selection box, the cracks -- has already been drawn.
    device.clearDepth();

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());

    m_buffer->bindBase(rhi::BufferTarget::Storage, kQuadBufferBinding);
    m_vao.bind();

    device.drawTriangles(static_cast<u32>(m_quads.size()) * kVerticesPerQuad);
}

} // namespace mc
