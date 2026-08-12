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
        /// Start borderless-fullscreen on the primary monitor at its own
        /// resolution. `width`/`height` still describe the windowed size to
        /// restore to, so a toggle back has somewhere to go.
        bool fullscreen = false;
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

    /// Moves between windowed and fullscreen on the monitor the window is on.
    ///
    /// **Fullscreen takes the monitor's own video mode**, so the framebuffer becomes
    /// the display's native resolution rather than being stretched from 1280x720.
    /// Everything downstream already follows: the framebuffer-size callback fires,
    /// `consumeResizeEvent` reports it, and the viewport, the projection's aspect
    /// ratio and the HUD layout are all derived from `framebufferWidth/Height` every
    /// frame. Nothing else has to know this happened.
    ///
    /// The windowed position and size are remembered on the way out and restored on
    /// the way back, because GLFW does not do it -- returning to a monitor with no
    /// size to go back to leaves a 0x0 window.
    void setFullscreen(bool enabled);
    bool fullscreen() const noexcept { return m_fullscreen; }
    void toggleFullscreen() { setFullscreen(!m_fullscreen); }

    int framebufferWidth() const noexcept { return m_framebufferWidth; }
    int framebufferHeight() const noexcept { return m_framebufferHeight; }

    /// True for exactly one frame after the framebuffer changed size.
    bool consumeResizeEvent();

    /// Internal to the platform module: the underlying GLFWwindow*, type-erased
    /// so this header stays free of GLFW. Only Input uses it. Anything outside
    /// `platform` that reaches for this is a layering mistake.
    void* nativeHandle() const;

private:
    /// How many event pumps a monitor switch waits for the framebuffer size to catch
    /// up. Wayland delivers the new size through a compositor round trip rather than
    /// synchronously; see the comment in `setFullscreen`.
    static constexpr int kResizeSettleAttempts = 16;
    /// Seconds to wait per attempt, so the whole budget is a sixth of a second.
    static constexpr double kResizeSettleSeconds = 0.01;

    struct Impl;
    Impl* m_impl = nullptr;

    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    bool m_resized = false;

    bool m_fullscreen = false;
    /// Vsync is re-applied after every monitor switch. The swap interval belongs to
    /// the surface, not to the context, and a driver is entitled to reset it when
    /// the surface is recreated underneath -- which is how a fullscreen toggle turns
    /// into an uncapped frame rate on some of them.
    bool m_vsync = true;
    /// Where to put the window back. Captured on the way into fullscreen.
    int m_windowedX = 0;
    int m_windowedY = 0;
    int m_windowedWidth = 0;
    int m_windowedHeight = 0;
};

} // namespace mc
