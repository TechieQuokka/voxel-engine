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
        {{-u(4), u(24), -u(4)}, {u(4), u(32), u(4)}, {}, 0.0f, kSkin, kHair},
        // Hair, as a shell over the top of the head rather than a colour on its
        // top face: from behind, a face-coloured head reads as a bald mannequin.
        // Inflated by a tenth of a unit so it cannot z-fight with the head.
        {{-u(4.1f), u(29), -u(4.1f)}, {u(4.1f), u(32.1f), u(4.1f)}, {}, 0.0f, kHair, kHair},
        // body: 8x12x4
        {{-u(4), u(12), -u(2)}, {u(4), u(24), u(2)}, {}, 0.0f, kShirt, kShirt},

        // Arms split at the sleeve. Two boxes rather than one is most of what
        // makes the silhouette read as a person rather than a stack of crates.
        {{-u(8), u(19), -u(2)}, {-u(4), u(24), u(2)}, shoulderR, 1.0f, kShirt, kShirt},
        {{-u(8), u(12), -u(2)}, {-u(4), u(19), u(2)}, shoulderR, 1.0f, kSkin, kSkin},
        {{u(4), u(19), -u(2)}, {u(8), u(24), u(2)}, shoulderL, -1.0f, kShirt, kShirt},
        {{u(4), u(12), -u(2)}, {u(8), u(19), u(2)}, shoulderL, -1.0f, kSkin, kSkin},

        // Legs swing against the arm on the same side, which is what walking is.
        {{-u(4), 0.0f, -u(2)}, {0.0f, u(12), u(2)}, hipR, -1.0f, kPants, kPants},
        {{0.0f, 0.0f, -u(2)}, {u(4), u(12), u(2)}, hipL, 1.0f, kPants, kPants},
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
                             f32 walkPhase, f32 walkAmount) {
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

    m_quads.clear();
    for (const Box& box : m_boxes) {
        appendBox(box, box.swing * amplitude * std::sin(walkPhase), feetPosition, right, up,
                  forward);
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

} // namespace mc
