#include "render/SelectionRenderer.hpp"

#include "core/Paths.hpp"
#include "core/Profile.hpp"

namespace mc {

SelectionRenderer::SelectionRenderer()
    : m_shader(rhi::Shader::fromFiles(assetPath("shaders/selection.vert"),
                                      assetPath("shaders/selection.frag"))) {}

void SelectionRenderer::draw(rhi::Device& device, const Camera& camera,
                             const vec3& blockOrigin) {
    MC_PROFILE_SCOPE_N("SelectionRenderer::draw");

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
    m_shader.setUniform("u_blockOrigin", blockOrigin);
    m_shader.setUniform("u_inflate", kInflate);

    // A VAO must still be bound for an attribute-less draw; this one describes
    // nothing, which is the point.
    m_vao.bind();
    device.drawLines(kVertexCount);
}

} // namespace mc
