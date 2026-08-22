#include "render/SelectionRenderer.hpp"

#include "core/Paths.hpp"
#include "core/Profile.hpp"

#include <cmath>
#include <vector>

namespace mc {
namespace {

/// Deterministic value hash, so the crack pattern is the same every run. A real
/// noise library is overkill for ten 16x16 stencils, exactly as in BlockTextures.
u32 hash2D(u32 x, u32 y, u32 seed) {
    u32 h = x * 0x9E3779B1u ^ y * 0x85EBCA77u ^ seed * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

f32 signedUnit(u32 hash) {
    return static_cast<f32>(hash & 0xFFFFu) / 32767.5f - 1.0f;
}

/// Generates the ten destroy stages, cumulatively.
///
/// **Cumulative is the point.** Branch `k` follows the same path whatever stage is
/// being drawn, and a stage simply draws more branches than the one before it, so
/// the cracks *spread* as mining progresses rather than being reshuffled ten times.
/// Ten unrelated random patterns would read as flickering, not as damage.
///
/// Each branch is a jagged walk outward from the middle of the tile. That is a lot
/// closer to how a crack looks than thresholded noise, which reads as dirt.
std::vector<u8> generateCrackStages(u32 size, u32 stageCount) {
    std::vector<u8> pixels(static_cast<usize>(size) * size * stageCount * 4, 0);

    const auto plot = [&](u32 stage, i32 x, i32 y) {
        if (x < 0 || y < 0 || x >= static_cast<i32>(size) || y >= static_cast<i32>(size)) {
            return;
        }
        const usize index =
            ((static_cast<usize>(stage) * size + static_cast<u32>(y)) * size
             + static_cast<u32>(x)) * 4;
        // **Linear, not sRGB, and the difference is four times too bright.** The
        // framebuffer is sRGB-encoded on write, so a value written here is a linear
        // one: 12/255 looks like a mid grey on screen, not the near-black it reads
        // as in source. 2/255 linear encodes to about 0.08 sRGB, which is the dark
        // this wants. Same rule as Device::clear -- see DESIGN.md 6.9.
        pixels[index + 0] = 2;
        pixels[index + 1] = 2;
        pixels[index + 2] = 2;
        // Not fully opaque: the block's own texture showing faintly through is what
        // says the cracks are *in* it rather than painted over it.
        pixels[index + 3] = 205;
    };

    const f32 centre = static_cast<f32>(size) * 0.5f;
    // Half the tile, so a branch reaches the edge and stops rather than meandering
    // back across ground it already covered.
    const u32 steps = size / 2u;

    for (u32 stage = 0; stage < stageCount; ++stage) {
        // One new branch per stage: one hairline at first, ten and shattered at the
        // end. The first version added two per stage over the tile's full width and
        // stage 7 covered 224 of 256 texels -- which does not read as cracks, it
        // reads as the block turning grey.
        const u32 branches = stage + 1;

        for (u32 branch = 0; branch < branches; ++branch) {
            // Spread the directions around the circle by branch index, so successive
            // branches do not clump on one side.
            const f32 angle = static_cast<f32>(branch) * 2.399963f
                            + signedUnit(hash2D(branch, 0u, 17u)) * 0.4f;

            f32 x = centre + signedUnit(hash2D(branch, 1u, 23u)) * 1.5f;
            f32 y = centre + signedUnit(hash2D(branch, 2u, 29u)) * 1.5f;

            f32 dx = std::cos(angle);
            f32 dy = std::sin(angle);

            for (u32 step = 0; step < steps; ++step) {
                // Wander, so the crack is jagged rather than a drawn radius.
                dx += signedUnit(hash2D(branch, step, 31u)) * 0.35f;
                dy += signedUnit(hash2D(branch, step, 37u)) * 0.35f;
                const f32 length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.0f) {
                    dx /= length;
                    dy /= length;
                }

                x += dx;
                y += dy;

                plot(stage, static_cast<i32>(std::floor(x)), static_cast<i32>(std::floor(y)));
            }
        }
    }

    return pixels;
}

} // namespace

SelectionRenderer::SelectionRenderer()
    : m_shader(rhi::Shader::fromFiles(assetPath("shaders/selection.vert"),
                                      assetPath("shaders/selection.frag"))),
      m_crackShader(rhi::Shader::fromFiles(assetPath("shaders/cracks.vert"),
                                           assetPath("shaders/cracks.frag"))) {
    const std::vector<u8> stages = generateCrackStages(kCrackSize, kStageCount);
    m_crackTextures = rhi::TextureArray::create(kCrackSize, kStageCount, stages,
                                                rhi::ColorSpace::Rgba8);
    m_crackShader.setUniform("u_cracks", static_cast<i32>(kCrackTextureUnit));
}

void SelectionRenderer::draw(rhi::Device& device, const Camera& camera,
                             const vec3& blockOrigin, f32 aspect,
                             const vec3& boxMin, const vec3& boxSize) {
    MC_PROFILE_SCOPE_N("SelectionRenderer::draw");

    m_shader.bind();
    m_shader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
    m_shader.setUniform("u_blockOrigin", blockOrigin);
    m_shader.setUniform("u_boxMin", boxMin);
    m_shader.setUniform("u_boxSize", boxSize);
    m_shader.setUniform("u_inflate", kInflate);
    m_shader.setUniform("u_thickness", kOutlineThickness);
    m_shader.setUniform("u_aspect", aspect);

    // A VAO must still be bound for an attribute-less draw; this one describes
    // nothing, which is the point.
    //
    // Triangles now, not lines: each edge is a quad. The outline still writes depth
    // and is still inflated just enough to win against its own block's faces, so a
    // wall in front of the target hides it exactly as before.
    m_vao.bind();
    device.drawTriangles(kVertexCount);
}

void SelectionRenderer::drawCracks(rhi::Device& device, const Camera& camera,
                                   const vec3& blockOrigin, u32 stage,
                                   const vec3& boxMin, const vec3& boxSize) {
    MC_PROFILE_SCOPE_N("SelectionRenderer::drawCracks");

    m_crackShader.bind();
    m_crackShader.setUniform("u_viewProjection", camera.viewProjectionMatrix());
    m_crackShader.setUniform("u_blockOrigin", blockOrigin);
    m_crackShader.setUniform("u_boxMin", boxMin);
    m_crackShader.setUniform("u_boxSize", boxSize);
    m_crackShader.setUniform("u_inflate", kInflate);
    m_crackShader.setUniform("u_stage", static_cast<i32>(stage));

    m_crackTextures->bind(kCrackTextureUnit);
    m_vao.bind();

    // Blended, and depth-write off: the overlay must be hidden by anything in front
    // of it while occluding nothing itself. Both are restored before returning, so
    // no other draw has to know this happened.
    device.setAlphaBlending(true);
    device.setDepthWrite(false);
    device.drawTriangles(kCrackVertexCount);
    device.setDepthWrite(true);
    device.setAlphaBlending(false);
}

} // namespace mc
