#include "render/ChunkRenderer.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "mesh/ModelBox.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace mc {

ChunkRenderer::ChunkRenderer() {
    m_shader = rhi::Shader::fromFiles(assetPath("shaders/chunk.vert"),
                                      assetPath("shaders/chunk.frag"));
    m_cutoutShader = rhi::Shader::fromFiles(assetPath("shaders/chunk.vert"),
                                            assetPath("shaders/glass.frag"));
    m_cutoutShader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));

    m_waterShader = rhi::Shader::fromFiles(assetPath("shaders/water.vert"),
                                           assetPath("shaders/water.frag"));

    m_modelShader = rhi::Shader::fromFiles(assetPath("shaders/model.vert"),
                                           assetPath("shaders/model.frag"));
    m_modelShader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));
    m_textures.emplace();
    m_shader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));
    m_waterShader.setUniform("u_blockTextures", static_cast<i32>(kTextureUnit));

    m_firsts.reserve(kInitialVisibleCapacity);
    m_counts.reserve(kInitialVisibleCapacity);
    m_origins.reserve(kInitialVisibleCapacity);
}

void ChunkRenderer::beginFrame() {
    m_firsts.clear();
    m_counts.clear();
    m_origins.clear();
    m_modelFirsts.clear();
    m_modelCounts.clear();
    m_modelOrigins.clear();
    m_cutoutFirsts.clear();
    m_cutoutCounts.clear();
    m_cutoutOrigins.clear();
    m_waterFirsts.clear();
    m_waterCounts.clear();
    m_waterOrigins.clear();
    m_stats = Stats{};
}

void ChunkRenderer::addSection(SectionPos pos, const SectionMeshStore::Placement& placement) {
    if (placement.quadCount == 0) {
        return;
    }

    const usize quadBase = placement.byteOffset / sizeof(Quad);
    const auto size = static_cast<f32>(kSectionSize);
    const vec4 origin{static_cast<f32>(pos.x) * size,
                      static_cast<f32>(pos.y) * size,
                      static_cast<f32>(pos.z) * size,
                      0.0f};

    // The three parts of a section's range become entries in three different draws,
    // so a section holding rock, a window and water appears once in each. Its origin
    // is therefore pushed to all three lists -- `gl_DrawID` counts within a draw, and
    // the shader adds the pass's base to reach the right one.
    if (placement.opaqueCount > 0) {
        m_firsts.push_back(static_cast<i32>(quadBase * kVerticesPerQuad));
        m_counts.push_back(static_cast<i32>(placement.opaqueCount * kVerticesPerQuad));
        m_origins.push_back(origin);
    }

    // **36 vertices a box, not 6 a quad**, which is why this count is not scaled by
    // the same constant as its neighbours. The words sit between the opaque stretch
    // and the cutout one.
    if (placement.modelCount > 0) {
        const usize modelBase = quadBase + placement.opaqueCount;
        m_modelFirsts.push_back(static_cast<i32>(modelBase * ModelBox::kVerticesPerBox));
        m_modelCounts.push_back(
            static_cast<i32>(placement.modelCount * ModelBox::kVerticesPerBox));
        m_modelOrigins.push_back(origin);
    }

    if (placement.cutoutCount > 0) {
        const usize cutoutBase = quadBase + placement.opaqueCount + placement.modelCount;
        m_cutoutFirsts.push_back(static_cast<i32>(cutoutBase * kVerticesPerQuad));
        m_cutoutCounts.push_back(
            static_cast<i32>(placement.cutoutCount * kVerticesPerQuad));
        m_cutoutOrigins.push_back(origin);
    }

    if (placement.translucentCount() > 0) {
        const usize waterBase = quadBase + placement.opaqueCount + placement.modelCount
                              + placement.cutoutCount;
        m_waterFirsts.push_back(static_cast<i32>(waterBase * kVerticesPerQuad));
        m_waterCounts.push_back(
            static_cast<i32>(placement.translucentCount() * kVerticesPerQuad));
        m_waterOrigins.push_back(origin);
    }

    ++m_stats.sectionsDrawn;
    m_stats.quadsDrawn += placement.quadCount;
    m_stats.modelBoxesDrawn += placement.modelCount;
    m_stats.cutoutQuadsDrawn += placement.cutoutCount;
    m_stats.waterQuadsDrawn += placement.translucentCount();
}

void ChunkRenderer::draw(rhi::Device& device, const Camera& camera,
                         const SectionMeshStore& store, rhi::FrameRing& ring) {
    MC_PROFILE_SCOPE_N("ChunkRenderer::draw");

    if (m_firsts.empty() && m_modelFirsts.empty() && m_cutoutFirsts.empty()
        && m_waterFirsts.empty()) {
        return;
    }

    // **One slice: opaque origins, then model, then cutout, then water.** Each pass
    // reaches its own stretch through `u_drawIdBase` rather than through a binding of
    // its own, so the four lists have to be contiguous -- which is why this reserves
    // once and writes four times instead of uploading each list separately.
    const usize originBytes = m_origins.size() * sizeof(vec4);
    const usize modelBytes = m_modelOrigins.size() * sizeof(vec4);
    const usize cutoutBytes = m_cutoutOrigins.size() * sizeof(vec4);
    const usize waterBytes = m_waterOrigins.size() * sizeof(vec4);

    const std::optional<rhi::FrameRing::Slice> slice =
        ring.reserve(originBytes + modelBytes + cutoutBytes + waterBytes);
    if (!slice.has_value()) {
        // The ring logs why. Skipping the terrain for one frame is visible and bad,
        // but it is a frame rather than a corrupted draw reading a neighbour's data.
        return;
    }

    ring.write(*slice, 0,
               std::span<const std::byte>{
                   reinterpret_cast<const std::byte*>(m_origins.data()), originBytes});

    if (!m_modelOrigins.empty()) {
        ring.write(*slice, originBytes,
                   std::span<const std::byte>{
                       reinterpret_cast<const std::byte*>(m_modelOrigins.data()),
                       modelBytes});
    }

    if (!m_cutoutOrigins.empty()) {
        ring.write(*slice, originBytes + modelBytes,
                   std::span<const std::byte>{
                       reinterpret_cast<const std::byte*>(m_cutoutOrigins.data()),
                       cutoutBytes});
    }

    if (!m_waterOrigins.empty()) {
        ring.write(*slice, originBytes + modelBytes + cutoutBytes,
                   std::span<const std::byte>{
                       reinterpret_cast<const std::byte*>(m_waterOrigins.data()), waterBytes});
    }

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
    ring.bind(rhi::BufferTarget::Storage, kSectionBufferBinding, *slice);
    m_vao.bind();

    m_shader.setUniform("u_drawIdBase", 0);
    if (!m_firsts.empty()) {
        device.multiDrawTriangles(m_firsts, m_counts);
    }

    // **The model pass next, still opaque and still writing depth.**
    //
    // A separate program only because the words are boxes rather than faces -- there
    // is no early-Z argument here as there is for glass, and nothing about it needs to
    // come after the terrain. It is second simply because both fill the same depth
    // buffer and the alpha test below has to test against a finished one.
    if (!m_modelFirsts.empty()) {
        m_modelShader.bind();
        m_modelShader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
        m_modelShader.setUniform("u_cameraPosition", camera.position());
        m_modelShader.setUniform("u_fadeDistance", m_fadeDistance);
        m_modelShader.setUniform("u_fogColor", m_fogColor);
        m_modelShader.setUniform("u_drawIdBase", static_cast<i32>(m_origins.size()));
        device.multiDrawTriangles(m_modelFirsts, m_modelCounts);
    }

    // **Glass third: after the depth buffer is filled, before anything blends.**
    //
    // Its own program purely so the opaque pass above keeps early-Z -- a shader that
    // can `discard` loses it for the whole draw, and paying that on all of the
    // terrain to draw a few windows is the trade this split exists to avoid. Depth
    // writes stay *on*: a pane is solid where it is not discarded, and something
    // behind it has to be occluded by the frame.
    if (!m_cutoutFirsts.empty()) {
        m_cutoutShader.bind();
        m_cutoutShader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
        m_cutoutShader.setUniform("u_cameraPosition", camera.position());
        m_cutoutShader.setUniform("u_aoStrength", m_aoStrength);
        m_cutoutShader.setUniform("u_fadeDistance", m_fadeDistance);
        m_cutoutShader.setUniform("u_fogColor", m_fogColor);
        m_cutoutShader.setUniform(
            "u_drawIdBase", static_cast<i32>(m_origins.size() + m_modelOrigins.size()));
        device.multiDrawTriangles(m_cutoutFirsts, m_cutoutCounts);
    }

    if (m_waterFirsts.empty()) {
        return;
    }

    // **Water second, blended, and with depth writes off.**
    //
    // After the opaque pass so it composites over a finished frame rather than over
    // whatever had been drawn when it got its turn. Depth *testing* stays on, so a
    // sea bed correctly hides the water below it and terrain in front of the ocean
    // still occludes it; depth *writing* goes off, because a translucent surface
    // that writes depth hides whatever is behind it -- including the rest of the
    // water -- and an ocean would render as its nearest 32-block section and nothing
    // else.
    //
    // **There is no back-to-front sort, and that is a real limitation rather than an
    // oversight.** Correct blending of overlapping translucent surfaces needs one.
    // Water gets away without it because the only translucent thing in the world is
    // water and it is very nearly a single flat sheet: two water surfaces are rarely
    // both in front of the same pixel. Looking through one ocean at another across a
    // bay is where this shows, and a second translucent block type is where it stops
    // being defensible.
    // **Back faces stay on for water**, which they are not for anything else.
    //
    // The mesher emits only the outside of a body of water -- every face between two
    // water blocks is culled against its own kind. So the surface of a lake is a
    // single sheet whose one face points up, and with back-face culling on it
    // vanishes the moment the camera goes under it: a swimmer would look up at open
    // sky. Vanilla draws water from both sides for the same reason.
    //
    // The cost is that looking across a lake can show its near wall and its far wall
    // blended together. With no back-to-front sort that is not strictly right, and it
    // is the same limitation noted above rather than a new one.
    device.setBackfaceCulling(false);
    device.setDepthWrite(false);
    device.setAlphaBlending(true);

    // A second program over the same arena and the same origin slice. It reads bits
    // 33..40 of a quad as four corner drops rather than as ambient occlusion, which
    // is why it cannot be the first program with a uniform flipped. See Quad.hpp.
    m_waterShader.bind();
    m_waterShader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
    m_waterShader.setUniform("u_cameraPosition", camera.position());
    m_waterShader.setUniform("u_fadeDistance", m_fadeDistance);
    m_waterShader.setUniform("u_fogColor", m_fogColor);
    m_waterShader.setUniform("u_time", m_time);
    m_waterShader.setUniform("u_drawIdBase",
                             static_cast<i32>(m_origins.size() + m_modelOrigins.size()
                                              + m_cutoutOrigins.size()));
    device.multiDrawTriangles(m_waterFirsts, m_waterCounts);

    device.setAlphaBlending(false);
    device.setDepthWrite(true);
    device.setBackfaceCulling(true);
}

} // namespace mc
