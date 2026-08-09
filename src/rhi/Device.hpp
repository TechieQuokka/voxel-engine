#pragma once

#include "core/Types.hpp"

#include <string>
#include <vector>

namespace mc::rhi {

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
    void clear(f32 r, f32 g, f32 b, f32 a);

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
