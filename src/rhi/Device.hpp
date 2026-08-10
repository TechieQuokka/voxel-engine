#pragma once

#include "core/Types.hpp"

#include <cmath>
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
