#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "render/BlockTextures.hpp"
#include "render/Camera.hpp"
#include "render/Frustum.hpp"
#include "render/SectionMeshStore.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/Device.hpp"
#include "rhi/FrameRing.hpp"
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
        /// Of `quadsDrawn`, how many were water. Separate because the translucent
        /// pass has different costs and different failure modes from the opaque one.
        usize waterQuadsDrawn = 0;
        /// Of `quadsDrawn`, how many were glass. Separate for the same reason and
        /// one more: the cutout pass is the only one that discards, so it is the one
        /// whose cost does not follow from its quad count alone.
        /// Model boxes, counted in boxes rather than quads -- each draws 36 vertices
        /// where a quad draws 6. Not part of `quadsDrawn` for that reason: adding
        /// them would make one number mean two different amounts of geometry.
        usize modelBoxesDrawn = 0;
        usize cutoutQuadsDrawn = 0;
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
    ///
    /// The origins go through the caller's frame ring rather than into a buffer this
    /// class owns. They are rewritten from scratch every frame, which is exactly the
    /// pattern that needs a ring: the previous frame's copy may still be being read.
    void draw(rhi::Device& device, const Camera& camera, const SectionMeshStore& store,
              rhi::FrameRing& ring);

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

    /// Seconds since start, for the water surface.
    ///
    /// **The only animated thing the renderer draws, and it is wall-clock rather
    /// than the 20 Hz tick on purpose.** A surface that advanced on the tick would
    /// visibly step at 20 Hz next to a camera running at several hundred frames a
    /// second; the simulation is what has to be deterministic, and this is not it.
    void setTime(f32 seconds) { m_time = seconds; }

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

    rhi::Shader m_shader;
    /// **Water is a second program over the same arena, not a branch in the first.**
    /// The translucent pass was already a separate multi-draw with its own
    /// `u_drawIdBase`, so the plumbing cost nothing -- and the two shaders disagree
    /// about what bits 33..40 of a quad mean, which is not something a uniform can
    /// express. See Quad.hpp.
    rhi::Shader m_waterShader;
    /// **Glass is a third program, and the reason is early-Z.** A fragment shader
    /// that can `discard` gives up the depth optimisation for the whole draw it is
    /// in, so an alpha test folded into `m_shader` would be paid on every block of
    /// terrain in the world to draw the handful of tiles that need it. It shares
    /// `chunk.vert` -- the geometry is identical; only the fragment rule differs.
    rhi::Shader m_cutoutShader;
    /// **The model pass -- non-cube geometry.** A fourth program because the words it
    /// reads are `ModelBox`es rather than `Quad`s: a box, not a face, expanded to 36
    /// vertices instead of 6. Same arena, same texture array, different decode. See
    /// mesh/ModelBox.hpp.
    rhi::Shader m_modelShader;
    rhi::VertexArray m_vao;
    std::optional<BlockTextures> m_textures;

public:
    /// The generated block textures, borrowed by anything else that has to draw a
    /// block: dropped items and the hotbar icons both do. One array rather than
    /// three copies, so a texture change cannot make a dropped stone stop matching
    /// the stone it came from.
    const BlockTextures& textures() const noexcept { return *m_textures; }

private:

    std::vector<i32> m_firsts;
    std::vector<i32> m_counts;
    std::vector<vec4> m_origins;

    /// The model pass -- slabs and the rest of the non-cube vocabulary. Drawn right
    /// after opaque, because it is depth-writing geometry of the same kind and the
    /// alpha test below has to see it in the depth buffer.
    std::vector<i32> m_modelFirsts;
    std::vector<i32> m_modelCounts;
    std::vector<vec4> m_modelOrigins;

    /// The cutout pass -- glass. Between opaque and translucent, because it writes
    /// depth like the first and must not be occluded by the second.
    std::vector<i32> m_cutoutFirsts;
    std::vector<i32> m_cutoutCounts;
    std::vector<vec4> m_cutoutOrigins;

    /// The translucent pass. A section with opaque geometry, glass and water appears
    /// in all three lists, because the three parts of its arena range are three
    /// draws.
    std::vector<i32> m_waterFirsts;
    std::vector<i32> m_waterCounts;
    std::vector<vec4> m_waterOrigins;

    f32 m_aoStrength = 1.0f;
    f32 m_fadeDistance = kDefaultFadeDistance;
    vec3 m_fogColor{1.0f};
    f32 m_time = 0.0f;
    Stats m_stats;
};

/// Per-frame budget for the shared frame ring, at a given render distance.
///
/// Derived rather than guessed, for `meshArenaBytesFor`'s reason: this is pinned,
/// persistently mapped memory that exists whether or not it is used, and a number
/// large enough for the eventual distance-64 target would be paid for at distance 8.
///
/// The section origins dominate and are the only term that grows: one `vec4` per
/// section per pass, and a section holding both terrain and water is an entry in
/// each. The bound is every section of every loaded column being visible in both
/// passes at once, which no frustum can actually contain -- so this is comfortably
/// above the worst real frame rather than merely at it.
///
/// The fixed part covers the other three renderers. The HUD is 1,024 quads of 48
/// bytes at its own hard limit (48 KiB), the character is 54 quads of 64 bytes drawn
/// at most twice (7 KiB), and dropped items and falling blocks share what is left at
/// 32 bytes each -- about 6,400 entities, against a few dozen in a mining session.
/// A frame that exceeds it loses a draw and logs, rather than dying.
inline usize frameRingBytesFor(i32 renderDistance) {
    constexpr usize kBytesPerSectionEntry = 2 * sizeof(vec4); // opaque pass + water pass
    constexpr usize kFixedSlack = 256u * 1024u;
    constexpr usize kMinimum = 1024u * 1024u;

    const auto side = static_cast<usize>(2 * (renderDistance > 0 ? renderDistance : 1) + 1);
    const usize sections = side * side * static_cast<usize>(kSectionsPerColumn);
    const usize estimate = sections * kBytesPerSectionEntry + kFixedSlack;

    return estimate < kMinimum ? kMinimum : estimate;
}

} // namespace mc
