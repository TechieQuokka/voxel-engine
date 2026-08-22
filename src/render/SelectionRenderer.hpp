#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/Camera.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/Texture.hpp"
#include "rhi/VertexArray.hpp"

#include <optional>

namespace mc {

/// Everything drawn on the block the player is pointing at: the wireframe box, and
/// the cracks that spread across it while it is being broken.
///
/// Neither is optional. Placing a block against a face whose edges you cannot see is
/// guesswork, and holding a mouse button for four seconds with no feedback is worse
/// than an instant break -- the player cannot tell the difference between "mining"
/// and "nothing is happening". Together they are the entire feedback loop for
/// interacting with the world, and they cost two draw calls with no vertex buffer
/// behind either: both shapes are constant tables in the shader, and the only
/// per-frame state is a block origin and a stage index in uniforms.
class SelectionRenderer {
public:
    SelectionRenderer();

    /// `blockOrigin` is the block's minimum corner in world space, i.e. its integer
    /// position. `boxMin` and `boxSize` are the shape inside that cell, as fractions
    /// of a block -- (0,0,0) and (1,1,1) for a full cube, which is what the defaults
    /// give.
    ///
    /// **The shape is a parameter because a slab is not a cube.** An outline that is
    /// always a unit cube says the player is aiming at a whole block when half of it
    /// is air, and aiming is the thing building rests on.
    void draw(rhi::Device& device, const Camera& camera, const vec3& blockOrigin,
              f32 aspect, const vec3& boxMin = vec3{0.0f},
              const vec3& boxSize = vec3{1.0f});

    /// Overlays destroy stage `stage` (0 to kStageCount - 1) on the same cube.
    ///
    /// Drawn blended, depth-tested but not depth-writing: the overlay has to be
    /// hidden by a wall in front of it and must not occlude anything itself.
    void drawCracks(rhi::Device& device, const Camera& camera, const vec3& blockOrigin,
                    u32 stage, const vec3& boxMin = vec3{0.0f},
                    const vec3& boxSize = vec3{1.0f});

    /// Destroy stages, matching vanilla's ten.
    static constexpr u32 kStageCount = 10;

    /// Maps break progress in [0, 1) to a stage.
    static constexpr u32 stageFor(f32 progress) {
        const auto stage = static_cast<i32>(progress * static_cast<f32>(kStageCount));
        return static_cast<u32>(math::clamp(stage, 0, static_cast<i32>(kStageCount) - 1));
    }

private:
    /// Twelve edges, **six vertices each**: every edge is a quad rather than a GL
    /// line, so the outline has a width that does not depend on whether the driver
    /// honours `glLineWidth`. See the header of selection.vert.
    static constexpr u32 kVertexCount = 12 * 6;

    /// Half-thickness of the outline, in NDC height units -- so roughly
    /// `kOutlineThickness * framebufferHeight` pixels wide on screen, at any
    /// distance within reach.
    ///
    /// Tuned against the complaint that produced this: a one-pixel outline could not
    /// be picked out of a textured world quickly enough to tell which block was
    /// about to be mined. Wider than this starts hiding the block's own edges.
    static constexpr f32 kOutlineThickness = 0.0015f;
    /// Six faces of two triangles.
    static constexpr u32 kCrackVertexCount = 36;
    /// Crack tiles are 16x16, like the block textures.
    static constexpr u32 kCrackSize = 16;
    static constexpr u32 kCrackTextureUnit = 0;

    /// How far the box is pushed out past the block's own surface, in blocks.
    ///
    /// Without it the outline lies exactly in the block's faces and z-fights along
    /// every edge, which flickers as the camera moves. Small enough not to read as a
    /// gap at arm's length, large enough to win the depth test at the far end of the
    /// reach.
    static constexpr f32 kInflate = 0.002f;

    rhi::Shader m_shader;
    rhi::Shader m_crackShader;
    rhi::VertexArray m_vao;
    std::optional<rhi::TextureArray> m_crackTextures;
};

} // namespace mc
