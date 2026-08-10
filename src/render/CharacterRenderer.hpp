#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/Camera.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

#include <array>
#include <optional>
#include <vector>

namespace mc {

/// Draws a blocky humanoid at a world position.
///
/// **This is the engine's second render path, and the first thing in it that is not
/// voxels.** A chunk `Quad` is six-bit integers on the block lattice; a player is
/// 0.5 blocks wide and its limbs swing, so none of that packing applies. What does
/// carry over is the shape of the solution: no vertex buffer, quads in an SSBO,
/// six corners expanded from `gl_VertexID`, one draw call. The character shader is
/// chunk.vert with floating-point edges substituted for the packed lattice, and it
/// shares chunk.frag's face-brightness table exactly so the model sits in the same
/// light as the ground under it.
///
/// The model is nine boxes in the proportions Minecraft uses -- an 8x8x8 head, an
/// 8x12x4 body, 4x12x4 limbs, at 16 units to the block. Arms are split at the
/// sleeve so the shirt can end partway down, and the hair is a shell over the top
/// of the head rather than a face colour, so it shows from behind. Both of those
/// are most of what makes the silhouette read as a person rather than a stack of
/// crates.
class CharacterRenderer {
public:
    CharacterRenderer();

    /// `facing` is the direction the character looks along, and needs no particular
    /// convention: the model is built from that vector and world up, so whatever
    /// the camera calls forward is what the character faces.
    ///
    /// `walkPhase` advances with distance travelled, not with time, so the limbs
    /// stop when the character does instead of marching on the spot. `walkAmount`
    /// in [0, 1] fades the swing in and out.
    void draw(rhi::Device& device, const Camera& camera, const vec3& feetPosition,
              const vec3& facing, f32 walkPhase, f32 walkAmount);

    /// Height of the model in blocks, from feet to the top of the head. The camera
    /// uses it to put the eye in the head rather than at the feet.
    static constexpr f32 kHeight = 2.0f;
    /// Eye height, matching Minecraft's 1.62.
    static constexpr f32 kEyeHeight = 1.62f;

private:
    /// One quad, laid out exactly as character.vert reads it.
    struct GpuQuad {
        vec4 origin;
        vec4 uAxis;
        vec4 vAxis;
        vec4 color;
    };

    /// One box of the model, in model space: x right, y up, z forward.
    struct Box {
        vec3 min;
        vec3 max;
        /// Point the box rotates about when it swings. Shoulders and hips.
        vec3 pivot;
        /// 0 for the head and body; +1 and -1 for limbs, so left and right swing
        /// in opposition.
        f32 swing;
        /// sRGB, decoded once at construction.
        u32 sideArgb;
        u32 topArgb;
    };

    static constexpr usize kBoxCount = 9;
    static constexpr usize kQuadsPerBox = 6;
    static constexpr usize kQuadCount = kBoxCount * kQuadsPerBox;
    static constexpr u32 kVerticesPerQuad = 6;
    static constexpr u32 kQuadBufferBinding = 0;

    void appendBox(const Box& box, f32 swingAngle, const vec3& feetPosition,
                   const vec3& right, const vec3& up, const vec3& forward);

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<rhi::Buffer> m_buffer;

    std::array<Box, kBoxCount> m_boxes;
    std::vector<GpuQuad> m_quads;
};

} // namespace mc
