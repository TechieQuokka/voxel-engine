#pragma once

#include "core/Types.hpp"

#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace mc::rhi {

/// Decodes one sRGB channel to linear.
///
/// The exact piecewise transfer function, not an approximate 2.2 power, because
/// this has to agree with the hardware's encode on framebuffer write -- and the
/// two curves differ by several percent in the dark end, which is where voxel
/// shading and ambient occlusion spend most of their range.
///
/// Lives here because the reason it is needed is GL_FRAMEBUFFER_SRGB, which
/// Device turns on.
inline f32 srgbToLinear(f32 channel) {
    return channel <= 0.04045f ? channel / 12.92f
                               : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

/// Owns the OpenGL function table and global driver state.
///
/// No GL type appears in this header -- handles are plain u32 (which is what
/// GLuint is). That constraint is what lets `mc_rhi` link glad privately, and
/// it is the seam a future Vulkan backend would be built against.
class Device {
public:
    /// Loads OpenGL entry points through `loader` and installs the debug
    /// message callback. Throws std::runtime_error if 4.6 is unavailable.
    explicit Device(GlProcLoader loader);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    const std::string& vendor() const noexcept { return m_vendor; }
    const std::string& renderer() const noexcept { return m_renderer; }
    const std::string& versionString() const noexcept { return m_version; }

    int versionMajor() const noexcept { return m_versionMajor; }
    int versionMinor() const noexcept { return m_versionMinor; }

    void setViewport(int x, int y, int width, int height);

    /// Clears colour and depth. The colour is **linear**, not sRGB: the
    /// framebuffer is sRGB-encoded on write, so a value picked from a colour
    /// picker has to be decoded before it gets here. See srgbToLinear below.
    void clear(f32 r, f32 g, f32 b, f32 a);

    void setDepthTest(bool enabled);
    /// Back faces are culled when enabled; quad winding is defined by the
    /// tangent basis in chunk.vert.
    void setBackfaceCulling(bool enabled);

    /// Attribute-less draw. Vertices are generated in the shader from
    /// gl_VertexID; a VertexArray must be bound.
    void drawTriangles(u32 vertexCount, u32 first = 0);

    /// The same, as line segments -- vertex pairs, one segment each.
    ///
    /// Here for the block selection outline, which is twelve edges and would
    /// otherwise have to be twelve thin boxes. Line width stays at the default 1.0:
    /// `glLineWidth` above 1.0 is not required to be supported in a core profile and
    /// is one of the few places where a driver will silently do nothing.
    void drawLines(u32 vertexCount, u32 first = 0);

    /// One GL call for many sections.
    ///
    /// `firsts` and `counts` are in vertices. Two properties make this work with
    /// the quad SSBO and are worth stating, because both are easy to get wrong:
    ///
    /// - `gl_VertexID` in OpenGL is absolute, already including `first`, so
    ///   `gl_VertexID / 6` indexes the shared arena directly with no base uniform.
    /// - `gl_DrawID` gives the shader its index into this list, which is how
    ///   per-section data is fetched without any per-draw uniform at all. It is
    ///   core in 4.6 (ARB_shader_draw_parameters).
    ///
    /// Phase 5 replaces this with the indirect form; the shader side does not
    /// change, because gl_DrawID means the same thing there.
    void multiDrawTriangles(std::span<const i32> firsts, std::span<const i32> counts);

    /// Reads the default framebuffer back as RGBA8, top row first.
    ///
    /// Stalls the pipeline, so this is a verification and debugging tool, not
    /// something the frame loop may call.
    std::vector<u8> readFramebufferRgba(int width, int height);

private:
    std::string m_vendor;
    std::string m_renderer;
    std::string m_version;
    int m_versionMajor = 0;
    int m_versionMinor = 0;
};

} // namespace mc::rhi
