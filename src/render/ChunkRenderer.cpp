#include "render/ChunkRenderer.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"

#include <cstddef>
#include <span>

namespace mc {

ChunkRenderer::ChunkRenderer() {
    m_shader = rhi::Shader::fromFiles(assetPath("shaders/chunk.vert"),
                                      assetPath("shaders/chunk.frag"));
    m_textures.emplace();
    m_shader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));

    m_firsts.reserve(kInitialVisibleCapacity);
    m_counts.reserve(kInitialVisibleCapacity);
    m_origins.reserve(kInitialVisibleCapacity);

    ensureSectionBuffer(kInitialVisibleCapacity);
}

void ChunkRenderer::beginFrame() {
    m_firsts.clear();
    m_counts.clear();
    m_origins.clear();
    m_stats = Stats{};
}

void ChunkRenderer::addSection(SectionPos pos, const SectionMeshStore::Placement& placement) {
    if (placement.quadCount == 0) {
        return;
    }

    const usize quadBase = placement.byteOffset / sizeof(Quad);

    m_firsts.push_back(static_cast<i32>(quadBase * kVerticesPerQuad));
    m_counts.push_back(static_cast<i32>(placement.quadCount * kVerticesPerQuad));

    const auto size = static_cast<f32>(kSectionSize);
    m_origins.push_back(vec4{static_cast<f32>(pos.x) * size,
                             static_cast<f32>(pos.y) * size,
                             static_cast<f32>(pos.z) * size,
                             0.0f});

    ++m_stats.sectionsDrawn;
    m_stats.quadsDrawn += placement.quadCount;
}

void ChunkRenderer::ensureSectionBuffer(usize sectionCount) {
    if (sectionCount <= m_sectionBufferCapacity) {
        return;
    }

    // Grow geometrically. A visible set that jumped in size once will do it again
    // when the camera turns back, and reallocating a GL buffer mid-frame is the
    // kind of hitch that shows up as a dropped frame rather than as a slow average.
    usize capacity = m_sectionBufferCapacity == 0 ? kInitialVisibleCapacity
                                                  : m_sectionBufferCapacity;
    while (capacity < sectionCount) {
        capacity *= 2;
    }

    m_sectionBuffer = rhi::Buffer::createPersistent(capacity * sizeof(vec4));
    m_sectionBufferCapacity = capacity;

    logDebug("Section origin buffer grown to {} entries ({} KiB)",
             capacity, capacity * sizeof(vec4) / 1024);
}

void ChunkRenderer::draw(rhi::Device& device, const Camera& camera, const SectionMeshStore& store) {
    MC_PROFILE_SCOPE_N("ChunkRenderer::draw");

    if (m_firsts.empty()) {
        return;
    }

    ensureSectionBuffer(m_origins.size());

    const std::span<const std::byte> originBytes{
        reinterpret_cast<const std::byte*>(m_origins.data()), m_origins.size() * sizeof(vec4)};
    m_sectionBuffer->write(0, originBytes);

    // Both the arena and the origin buffer are persistently mapped and were written
    // from the CPU, so the GPU has to be told to observe those writes. Coherence
    // removed the need to flush, not the need to order.
    rhi::Buffer::barrierAfterClientWrites();

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
    m_shader.setUniform("u_cameraPosition", camera.position());
    m_shader.setUniform("u_aoStrength", m_aoStrength);
    m_shader.setUniform("u_fadeDistance", m_fadeDistance);
    m_shader.setUniform("u_fogColor", m_fogColor);

    m_textures->bind(kTextureUnit);
    store.buffer().bindBase(rhi::BufferTarget::Storage, kQuadBufferBinding);
    m_sectionBuffer->bindBase(rhi::BufferTarget::Storage, kSectionBufferBinding);
    m_vao.bind();

    device.multiDrawTriangles(m_firsts, m_counts);
}

} // namespace mc
