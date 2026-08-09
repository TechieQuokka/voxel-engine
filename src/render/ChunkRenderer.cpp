#include "render/ChunkRenderer.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace mc {
namespace {

/// Must match `layout(binding = ...)` in chunk.vert.
constexpr u32 kQuadBufferBinding = 0;

/// Six vertices per quad: two triangles, no index buffer. The vertex shader
/// derives both the corner and the triangle from gl_VertexID.
constexpr u32 kVerticesPerQuad = 6;

} // namespace

ChunkRenderer::ChunkRenderer() {
    m_shader = rhi::Shader::fromFiles(assetPath("shaders/chunk.vert"),
                                      assetPath("shaders/chunk.frag"));
    uploadBlockColors();
}

void ChunkRenderer::uploadBlockColors() {
    // Sourced from BlockRegistry so the shader never becomes a second place
    // where block appearance is defined. Replaced by the texture array in
    // Phase 2.
    const BlockRegistry& blocks = BlockRegistry::instance();

    std::vector<vec4> colors(kMaxDebugColors, vec4(1.0f, 0.0f, 1.0f, 1.0f));
    for (usize i = 0; i < blocks.size() && i < kMaxDebugColors; ++i) {
        const u32 argb = blocks.all()[i].debugColor;
        colors[i] = vec4(static_cast<f32>((argb >> 16) & 0xFFu) / 255.0f,
                         static_cast<f32>((argb >> 8) & 0xFFu) / 255.0f,
                         static_cast<f32>(argb & 0xFFu) / 255.0f,
                         static_cast<f32>((argb >> 24) & 0xFFu) / 255.0f);
    }

    m_shader.setUniform("u_blockColors", std::span<const vec4>(colors));
}

void ChunkRenderer::upload(const ChunkMesh& mesh) {
    MC_PROFILE_SCOPE_N("ChunkRenderer::upload");

    m_quadCount = mesh.quadCount();
    if (m_quadCount == 0) {
        m_quadBuffer.reset();
        return;
    }

    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(mesh.quads.data()), mesh.byteSize()};
    m_quadBuffer = rhi::Buffer::createStatic(bytes);

    logDebug("Uploaded {} quads ({} KiB)", m_quadCount, mesh.byteSize() / 1024);
}

void ChunkRenderer::draw(rhi::Device& device, const Camera& camera, const vec3& sectionOrigin) {
    MC_PROFILE_SCOPE_N("ChunkRenderer::draw");

    if (m_quadCount == 0 || !m_quadBuffer.has_value()) {
        return;
    }

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
    m_shader.setUniform("u_sectionOrigin", sectionOrigin);
    m_shader.setUniform("u_cameraPosition", camera.position());

    m_quadBuffer->bindBase(rhi::BufferTarget::Storage, kQuadBufferBinding);
    m_vao.bind();

    device.drawTriangles(static_cast<u32>(m_quadCount) * kVerticesPerQuad);
}

} // namespace mc
