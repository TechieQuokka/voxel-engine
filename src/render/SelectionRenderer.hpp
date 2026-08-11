#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/Camera.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

namespace mc {

/// The wireframe box around the block the player is pointing at.
///
/// Small, and not optional. Placing a block against a face you cannot see the edges
/// of is guesswork -- you find out where it went after it is there. This is the
/// entire feedback loop for building, and it costs one draw call of twenty-four
/// vertices with no buffer behind it at all: the cube's edges are a constant table
/// in the shader, and the only per-frame state is the block's origin in a uniform.
class SelectionRenderer {
public:
    SelectionRenderer();

    /// `blockOrigin` is the block's minimum corner in world space, i.e. its integer
    /// position. The box drawn is the unit cube from there.
    void draw(rhi::Device& device, const Camera& camera, const vec3& blockOrigin);

private:
    /// Twelve edges, two vertices each.
    static constexpr u32 kVertexCount = 24;

    /// How far the box is pushed out past the block's own surface, in blocks.
    ///
    /// Without it the outline lies exactly in the block's faces and z-fights along
    /// every edge, which flickers as the camera moves. Small enough not to read as a
    /// gap at arm's length, large enough to win the depth test at the far end of the
    /// reach.
    static constexpr f32 kInflate = 0.002f;

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
};

} // namespace mc
