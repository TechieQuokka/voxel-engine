#include "rhi/Device.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace mc::rhi {
namespace {

std::string glString(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value != nullptr ? std::string(reinterpret_cast<const char*>(value)) : std::string("<unknown>");
}

const char* sourceName(GLenum source) {
    switch (source) {
    case GL_DEBUG_SOURCE_API:             return "api";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   return "window-system";
    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader-compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY:     return "third-party";
    case GL_DEBUG_SOURCE_APPLICATION:     return "application";
    default:                              return "other";
    }
}

const char* typeName(GLenum type) {
    switch (type) {
    case GL_DEBUG_TYPE_ERROR:               return "error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  return "undefined-behaviour";
    case GL_DEBUG_TYPE_PORTABILITY:         return "portability";
    case GL_DEBUG_TYPE_PERFORMANCE:         return "performance";
    case GL_DEBUG_TYPE_MARKER:              return "marker";
    default:                                return "other";
    }
}

void GLAD_API_PTR debugCallback(GLenum source,
                                GLenum type,
                                GLuint id,
                                GLenum severity,
                                GLsizei length,
                                const GLchar* message,
                                const void* /*userParam*/) {
    const std::string_view text(message, static_cast<std::size_t>(length));

    LogLevel level = LogLevel::Info;
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:         level = LogLevel::Error; break;
    case GL_DEBUG_SEVERITY_MEDIUM:       level = LogLevel::Warn;  break;
    case GL_DEBUG_SEVERITY_LOW:          level = LogLevel::Warn;  break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: level = LogLevel::Trace; break;
    default:                             level = LogLevel::Info;  break;
    }

    log(level, "GL [{}/{}] ({}) {}", sourceName(source), typeName(type), id, text);
}

} // namespace

Device::Device(GlProcLoader loader) {
    const int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(loader));
    if (version == 0) {
        throw std::runtime_error("Failed to load OpenGL entry points");
    }

    m_versionMajor = GLAD_VERSION_MAJOR(version);
    m_versionMinor = GLAD_VERSION_MINOR(version);

    if (m_versionMajor < 4 || (m_versionMajor == 4 && m_versionMinor < 6)) {
        throw std::runtime_error(std::format(
            "OpenGL 4.6 is required, but the driver reports {}.{}",
            m_versionMajor, m_versionMinor));
    }

    m_vendor   = glString(GL_VENDOR);
    m_renderer = glString(GL_RENDERER);
    m_version  = glString(GL_VERSION);

    logInfo("OpenGL {}.{} | {} | {}", m_versionMajor, m_versionMinor, m_renderer, m_vendor);

    // Reversed-Z setup, applied once for the lifetime of the context: clip
    // space depth in [0, 1] instead of [-1, 1], cleared to 0, compared with
    // GREATER. Without this, depth precision at a 1024-block render distance is
    // not sufficient to avoid z-fighting. See Camera::setPerspective.
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glClearDepth(0.0);

    // sRGB-correct output, also applied once for the context lifetime. Shaders
    // sample sRGB textures into linear values, do their arithmetic there, and
    // this converts the result back on write to the framebuffer. Multiplying
    // sRGB-encoded values directly -- which is what happens without this -- is
    // simply the wrong operation for anything that behaves like light, and the
    // shading here is all multiplicative.
    //
    // Consequence for every caller: colours handed to clear() are now linear.
    glEnable(GL_FRAMEBUFFER_SRGB);

    GLint contextFlags = 0;
    glGetIntegerv(GL_CONTEXT_FLAGS, &contextFlags);
    if ((static_cast<GLuint>(contextFlags) & GLuint{GL_CONTEXT_FLAG_DEBUG_BIT}) != 0) {
        glEnable(GL_DEBUG_OUTPUT);
        // Synchronous so the reported call site is the one that actually
        // caused the message. The cost only matters in debug builds.
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(&debugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        logInfo("GL debug output enabled");
    } else {
        logWarn("No debug context available; GL errors will be silent");
    }
}

void Device::setViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void Device::clear(f32 r, f32 g, f32 b, f32 a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Device::setDepthTest(bool enabled) {
    if (enabled) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER); // Reversed-Z: larger depth means nearer.
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void Device::setBackfaceCulling(bool enabled) {
    if (enabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void Device::drawTriangles(u32 vertexCount, u32 first) {
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first), static_cast<GLsizei>(vertexCount));
}

void Device::multiDrawTriangles(std::span<const i32> firsts, std::span<const i32> counts) {
    MC_ASSERT(firsts.size() == counts.size());
    if (firsts.empty()) {
        return;
    }

    glMultiDrawArrays(GL_TRIANGLES, firsts.data(), counts.data(),
                      static_cast<GLsizei>(firsts.size()));
}

std::vector<u8> Device::readFramebufferRgba(int width, int height) {
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<u8> pixels(pixelCount * 4);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // GL returns rows bottom-up; flip so row 0 is the top of the image.
    const auto stride = static_cast<std::size_t>(width) * 4;
    for (int row = 0; row < height / 2; ++row) {
        const auto top = static_cast<std::size_t>(row) * stride;
        const auto bottom = static_cast<std::size_t>(height - 1 - row) * stride;
        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(top),
                         pixels.begin() + static_cast<std::ptrdiff_t>(top + stride),
                         pixels.begin() + static_cast<std::ptrdiff_t>(bottom));
    }
    return pixels;
}

} // namespace mc::rhi
