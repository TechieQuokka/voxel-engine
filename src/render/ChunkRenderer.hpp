#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "render/BlockTextures.hpp"
#include "render/Camera.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

#include <optional>

namespace mc {

/// Draws packed quads.
///
/// Phase 2 renders one section from a single static buffer, textured from an
/// array texture. The upload path becomes persistent-mapped and the draw
/// becomes indirect in Phases 3 and 5; what stays fixed is that no vertex
/// buffer is ever involved -- quads are read from an SSBO and expanded in the
/// vertex shader.
class ChunkRenderer {
public:
    ChunkRenderer();

    /// Replaces the mesh currently on the GPU.
    void upload(const ChunkMesh& mesh);

    /// `sectionOrigin` is the section's world-space corner in blocks.
    void draw(rhi::Device& device, const Camera& camera, const vec3& sectionOrigin);

    /// 0 disables ambient occlusion shading without remeshing. Useful for
    /// judging what AO actually contributes visually against what it costs in
    /// merge ratio.
    void setAoStrength(f32 strength) { m_aoStrength = strength; }
    f32 aoStrength() const noexcept { return m_aoStrength; }

    /// Distance in blocks at which the stand-in distance darkening bottoms out.
    /// Streaming in Phase 3 derives this from the render distance instead of
    /// leaving it at the default.
    void setFadeDistance(f32 blocks) { m_fadeDistance = blocks; }
    f32 fadeDistance() const noexcept { return m_fadeDistance; }

    usize quadCount() const noexcept { return m_quadCount; }

private:
    static constexpr u32 kTextureUnit = 0;

    /// Comfortably beyond a single section, so nothing visible is affected while
    /// only one is rendered.
    static constexpr f32 kDefaultFadeDistance = 400.0f;

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<BlockTextures> m_textures;
    std::optional<rhi::Buffer> m_quadBuffer;
    usize m_quadCount = 0;
    f32 m_aoStrength = 1.0f;
    f32 m_fadeDistance = kDefaultFadeDistance;
};

} // namespace mc
