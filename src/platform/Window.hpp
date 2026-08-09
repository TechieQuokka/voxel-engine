#pragma once

#include "core/Types.hpp"

#include <string_view>

namespace mc {

/// An OS window with an OpenGL 4.6 core context.
///
/// No GLFW type appears in this header. That is what allows `mc_platform` to
/// link GLFW privately, which in turn means the windowing backend can be
/// replaced without touching any other module.
class Window {
public:
    struct Config {
        int width = 1280;
        int height = 720;
        std::string_view title = "minecraft";
        bool vsync = true;
        /// Requests a debug context so the driver can report errors through
        /// GL_DEBUG_OUTPUT. Costs nothing measurable outside of profiling runs.
        bool debugContext = true;
    };

    explicit Window(const Config& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// Loader for OpenGL entry points, handed to rhi::Device. Returning a
    /// plain function pointer keeps `rhi` independent of `platform`.
    static GlProcLoader glProcLoader();

    bool shouldClose() const;
    void requestClose();

    void pollEvents();
    void swapBuffers();

    void setVsync(bool enabled);

    int framebufferWidth() const noexcept { return m_framebufferWidth; }
    int framebufferHeight() const noexcept { return m_framebufferHeight; }

    /// True for exactly one frame after the framebuffer changed size.
    bool consumeResizeEvent();

private:
    struct Impl;
    Impl* m_impl = nullptr;

    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    bool m_resized = false;
};

} // namespace mc
