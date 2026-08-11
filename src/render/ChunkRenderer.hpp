#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "render/Camera.hpp"
#include "render/Frustum.hpp"
#include "render/SectionMeshStore.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"
#include "world/Coords.hpp"

#include <optional>
#include <vector>

namespace mc {

/// Draws every visible section with one GL call.
///
/// No vertex buffer is involved anywhere: quads are read from the mesh store's
/// arena and expanded in the vertex shader, and each draw finds its section origin
/// through `gl_DrawID`. So there are no per-draw uniforms at all, which is what
/// makes thousands of sections per frame affordable -- the previous shape of this
/// class set five uniforms per section, and `glGetUniformLocation` on each.
///
/// Phase 5 swaps glMultiDrawArrays for the indirect form with a compute shader
/// filling the command buffer. The shader does not change: gl_DrawID means the same
/// thing there, which is why the visible-set list is built in exactly this layout.
class ChunkRenderer {
public:
    struct Stats {
        usize sectionsConsidered = 0;
        usize columnsCulled = 0;
        usize sectionsCulled = 0;
        usize sectionsDrawn = 0;
        usize quadsDrawn = 0;
    };

    ChunkRenderer();

    /// Clears the visible set. Call once per frame before adding to it.
    void beginFrame();

    /// Adds one section, if it has a mesh. Culling has already happened by here --
    /// the caller does the hierarchical test, because it is the one walking columns.
    void addSection(SectionPos pos, const SectionMeshStore::Placement& placement);

    /// Uploads the visible set and issues the draw.
    void draw(rhi::Device& device, const Camera& camera, const SectionMeshStore& store);

    /// 0 disables ambient occlusion shading without remeshing. Useful for
    /// judging what AO actually contributes visually against what it costs in
    /// merge ratio.
    void setAoStrength(f32 strength) { m_aoStrength = strength; }
    f32 aoStrength() const noexcept { return m_aoStrength; }

    /// Distance in blocks at which terrain has fully faded into the fog. Streaming
    /// sets it from the render distance, so the loaded region ends out of sight
    /// rather than at a visible edge.
    void setFadeDistance(f32 blocks) { m_fadeDistance = blocks; }
    f32 fadeDistance() const noexcept { return m_fadeDistance; }

    /// Must match the clear colour, and must be linear -- see DESIGN.md 6.9.
    void setFogColor(const vec3& linearColor) { m_fogColor = linearColor; }

    Stats& stats() noexcept { return m_stats; }
    const Stats& stats() const noexcept { return m_stats; }

private:
    static constexpr u32 kTextureUnit = 0;
    static constexpr u32 kQuadBufferBinding = 0;
    static constexpr u32 kSectionBufferBinding = 1;

    /// Six vertices per quad: two triangles, no index buffer.
    static constexpr u32 kVerticesPerQuad = 6;

    /// Comfortably beyond a single section, so nothing visible is affected while
    /// only one is rendered. Streaming overrides it from the render distance.
    static constexpr f32 kDefaultFadeDistance = 400.0f;

    /// Grown once and then reused; the visible set is rebuilt every frame and
    /// reallocating it per frame would be pure waste.
    static constexpr usize kInitialVisibleCapacity = 4096;

    void ensureSectionBuffer(usize sectionCount);

    rhi::Shader m_shader;
    rhi::VertexArray m_vao;
    std::optional<BlockTextures> m_textures;

public:
    /// The generated block textures, borrowed by anything else that has to draw a
    /// block: dropped items and the hotbar icons both do. One array rather than
    /// three copies, so a texture change cannot make a dropped stone stop matching
    /// the stone it came from.
    const BlockTextures& textures() const noexcept { return *m_textures; }

private:

    /// Per-section origins, persistently mapped so the visible set can be written
    /// without a GL call per frame.
    std::optional<rhi::Buffer> m_sectionBuffer;
    usize m_sectionBufferCapacity = 0;

    std::vector<i32> m_firsts;
    std::vector<i32> m_counts;
    std::vector<vec4> m_origins;

    f32 m_aoStrength = 1.0f;
    f32 m_fadeDistance = kDefaultFadeDistance;
    vec3 m_fogColor{1.0f};
    Stats m_stats;
};

} // namespace mc
