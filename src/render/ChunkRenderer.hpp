#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "render/Camera.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

#include <optional>

namespace mc {

/// Draws packed quads.
///
/// Phase 1 renders exactly one section from a single static buffer. The upload
/// path becomes persistent-mapped and the draw becomes indirect in Phase 3 and
/// 5; what stays fixed is that no vertex buffer is ever involved -- quads are
/// read from an SSBO and expanded in the vertex shader.
class ChunkRenderer {
public:
    ChunkRenderer();

    /// Replaces the mesh currently on the GPU.
    void upload(const ChunkMesh& mesh);

    /// `sectionOrigin` is the section's world-space corner in blocks.
    void draw(rhi::Device& device, const Camera& camera, const vec3& sectionOrigin);

    usize quadCount() const noexcept { return m_quadCount; }

private:
    /// Matches the u_blockColors array size in chunk.frag.
    static constexpr usize kMaxDebugColors = 16;

    void uploadBlockColors();

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<rhi::Buffer> m_quadBuffer;
    usize m_quadCount = 0;
};

} // namespace mc
