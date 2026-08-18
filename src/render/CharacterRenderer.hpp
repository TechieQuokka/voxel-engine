#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/Camera.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"
#include "world/PlayerBox.hpp"

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
    /// `swingPhase` advances with time while mining and `swingAmount` in [0, 1]
    /// fades the chop in and out. The right arm follows the swing instead of the
    /// walk cycle while it is up; every other limb keeps walking, which is what
    /// mining while moving looks like.
    void draw(rhi::Device& device, const Camera& camera, const vec3& feetPosition,
              const vec3& facing, f32 walkPhase, f32 walkAmount,
              f32 swingPhase, f32 swingAmount);

    /// The first-person arm, at the bottom right of the view.
    ///
    /// **Built in world space against the camera basis, not in a separate view-space
    /// projection.** Placing it a fixed offset from the eye and drawing it with the
    /// ordinary view-projection means it reuses this class's shader, quad format and
    /// buffer exactly, and needs no second projection matrix to keep in step with
    /// the first. What it does need is a depth clear first, or the arm intersects
    /// any wall the player stands against -- which is what `Device::clearDepth` is
    /// for and what Minecraft does too.
    void drawHand(rhi::Device& device, const Camera& camera, f32 swingPhase,
                  f32 swingAmount);

    /// Height of the model in blocks, from feet to the top of the head, and the eye
    /// height the camera uses to sit in the head rather than at the feet.
    ///
    /// **Both come from `PlayerBox` rather than being declared here.** They used to be
    /// declared here, which put the player's dimensions in the renderer and left
    /// physics reading them out of it -- and the collision box that grew from that had
    /// a height and no width. The model is drawn to match what collides, not the other
    /// way round, so these are aliases and cannot drift from the box.
    static constexpr f32 kHeight = PlayerBox::kHeight;
    static constexpr f32 kEyeHeight = PlayerBox::kEyeHeight;

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
        /// True for the two boxes of the right arm, which is the one that mines.
        /// A flag rather than an index comparison so reordering the model cannot
        /// silently animate an elbow.
        bool rightArm;
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
