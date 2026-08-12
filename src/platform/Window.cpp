#include "platform/Window.hpp"

#include "core/Log.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>

namespace mc {
namespace {

int g_glfwRefCount = 0;

void glfwErrorCallback(int code, const char* description) {
    logError("GLFW error {}: {}", code, description);
}

void acquireGlfw() {
    if (g_glfwRefCount == 0) {
        glfwSetErrorCallback(&glfwErrorCallback);
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit failed");
        }
    }
    ++g_glfwRefCount;
}

void releaseGlfw() {
    if (--g_glfwRefCount == 0) {
        glfwTerminate();
    }
}

/// **Wayland has no window position, by design.** There is no protocol for a client
/// to ask where its own surface is, and GLFW reports that as an error rather than a
/// zero -- so calling `glfwGetWindowPos` there logs `65548` and yields nothing. Every
/// position query in this file is guarded by this, and the fallbacks are what make
/// the whole feature work on the platform this project targets.
bool hasWindowPosition() {
    return glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
}

/// The monitor the window is most on, which is not always the primary one.
///
/// GLFW only offers "which monitor is this *fullscreen* window on", so a windowed
/// one has to be placed by hand: take the monitor whose work area overlaps the
/// window rectangle most. Going fullscreen on the primary monitor regardless is what
/// makes multi-monitor setups jump the window to the wrong screen.
///
/// On Wayland the overlap cannot be computed, so this is the primary monitor and the
/// compositor decides -- which is also the platform's own answer: the surface goes
/// fullscreen wherever the compositor already had it.
GLFWmonitor* monitorForWindow(GLFWwindow* handle) {
    if (!hasWindowPosition()) {
        return glfwGetPrimaryMonitor();
    }

    int windowX = 0;
    int windowY = 0;
    int windowWidth = 0;
    int windowHeight = 0;
    glfwGetWindowPos(handle, &windowX, &windowY);
    glfwGetWindowSize(handle, &windowWidth, &windowHeight);

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    if (monitors == nullptr || count == 0) {
        return glfwGetPrimaryMonitor();
    }

    GLFWmonitor* best = glfwGetPrimaryMonitor();
    int bestOverlap = 0;

    for (int i = 0; i < count; ++i) {
        int areaX = 0;
        int areaY = 0;
        int areaWidth = 0;
        int areaHeight = 0;
        glfwGetMonitorWorkarea(monitors[i], &areaX, &areaY, &areaWidth, &areaHeight);

        const int overlapX = std::max(0, std::min(windowX + windowWidth, areaX + areaWidth)
                                             - std::max(windowX, areaX));
        const int overlapY = std::max(0, std::min(windowY + windowHeight, areaY + areaHeight)
                                             - std::max(windowY, areaY));
        const int overlap = overlapX * overlapY;

        if (overlap > bestOverlap) {
            bestOverlap = overlap;
            best = monitors[i];
        }
    }

    return best;
}

} // namespace

struct Window::Impl {
    GLFWwindow* handle = nullptr;
};

Window::Window(const Config& config) {
    acquireGlfw();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, config.debugContext ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    const std::string title(config.title);
    GLFWwindow* handle =
        glfwCreateWindow(config.width, config.height, title.c_str(), nullptr, nullptr);
    if (handle == nullptr) {
        releaseGlfw();
        throw std::runtime_error(std::format(
            "Failed to create a {}x{} OpenGL 4.6 core window. "
            "The driver may not support OpenGL 4.6.",
            config.width, config.height));
    }

    m_impl = new Impl{handle};
    glfwSetWindowUserPointer(handle, this);
    glfwMakeContextCurrent(handle);

    glfwGetFramebufferSize(handle, &m_framebufferWidth, &m_framebufferHeight);

    glfwSetFramebufferSizeCallback(handle, [](GLFWwindow* w, int width, int height) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->m_framebufferWidth = width;
        self->m_framebufferHeight = height;
        self->m_resized = true;
    });

    // Where a toggle out of fullscreen goes. Seeded from the config rather than
    // queried, so it is valid even when the window starts fullscreen and has never
    // been windowed.
    m_windowedWidth = config.width;
    m_windowedHeight = config.height;
    if (hasWindowPosition()) {
        glfwGetWindowPos(handle, &m_windowedX, &m_windowedY);
    }

    setVsync(config.vsync);

    logInfo("Window created: {}x{} (framebuffer {}x{})",
            config.width, config.height, m_framebufferWidth, m_framebufferHeight);

    if (config.fullscreen) {
        setFullscreen(true);
    }
}

Window::~Window() {
    if (m_impl != nullptr) {
        glfwDestroyWindow(m_impl->handle);
        delete m_impl;
        m_impl = nullptr;
        releaseGlfw();
    }
}

GlProcLoader Window::glProcLoader() {
    // GLFWglproc and mc::GlProc are both `void (*)()`, so this is a plain
    // function-pointer conversion rather than a reinterpretation.
    return reinterpret_cast<GlProcLoader>(&glfwGetProcAddress);
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_impl->handle) == GLFW_TRUE;
}

void Window::requestClose() {
    glfwSetWindowShouldClose(m_impl->handle, GLFW_TRUE);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::swapBuffers() {
    glfwSwapBuffers(m_impl->handle);
}

void Window::setVsync(bool enabled) {
    m_vsync = enabled;
    glfwSwapInterval(enabled ? 1 : 0);
}

void Window::setFullscreen(bool enabled) {
    if (enabled == m_fullscreen) {
        return;
    }

    GLFWwindow* handle = m_impl->handle;

    if (enabled) {
        // Remembered before the move, because afterwards GLFW reports the monitor's
        // geometry and the windowed rectangle is gone.
        if (hasWindowPosition()) {
            glfwGetWindowPos(handle, &m_windowedX, &m_windowedY);
        }
        glfwGetWindowSize(handle, &m_windowedWidth, &m_windowedHeight);

        GLFWmonitor* monitor = monitorForWindow(handle);
        const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
        if (monitor == nullptr || mode == nullptr) {
            logError("Fullscreen requested with no usable monitor; staying windowed");
            return;
        }

        // The monitor's own mode, so the framebuffer is the display's native
        // resolution rather than 1280x720 scaled up to it.
        glfwSetWindowMonitor(handle, monitor, 0, 0, mode->width, mode->height,
                             mode->refreshRate);
        m_fullscreen = true;
        logInfo("Fullscreen: {}x{} at {} Hz on \"{}\"", mode->width, mode->height,
                mode->refreshRate, glfwGetMonitorName(monitor));
    } else {
        glfwSetWindowMonitor(handle, nullptr, m_windowedX, m_windowedY,
                             m_windowedWidth, m_windowedHeight, GLFW_DONT_CARE);
        m_fullscreen = false;
        logInfo("Windowed: {}x{}", m_windowedWidth, m_windowedHeight);
    }

    // The surface was recreated underneath the context, so re-assert the swap
    // interval: it belongs to the surface, not to the context.
    setVsync(m_vsync);

    // **The new size does not exist yet on Wayland, and reading it here gets the old
    // one.** The resize is a round trip -- the compositor sends a configure event and
    // the client acknowledges it -- so `glfwGetFramebufferSize` immediately after the
    // switch returns the pre-switch size. Interactively that corrects itself on the
    // next frame and nobody notices; `--capture --fullscreen` does not get a next
    // frame, and captured a 1280x720 image of a 2560x1440 fullscreen window.
    //
    // So pump events until the size settles. Bounded, because a compositor that never
    // resizes must not hang the toggle -- the callback stays authoritative either way,
    // and the worst case is one frame drawn at the old size.
    const int beforeWidth = m_framebufferWidth;
    const int beforeHeight = m_framebufferHeight;

    for (int attempt = 0; attempt < kResizeSettleAttempts; ++attempt) {
        // Waiting rather than polling: the reply has to come back from the
        // compositor, and a tight `glfwPollEvents` loop spins through the whole
        // budget before it can possibly have arrived.
        glfwWaitEventsTimeout(kResizeSettleSeconds);
        glfwGetFramebufferSize(handle, &m_framebufferWidth, &m_framebufferHeight);
        if (m_framebufferWidth != beforeWidth || m_framebufferHeight != beforeHeight) {
            break;
        }
    }

    m_resized = true;
    logInfo("Framebuffer now {}x{}", m_framebufferWidth, m_framebufferHeight);
}

void* Window::nativeHandle() const {
    return m_impl->handle;
}

bool Window::consumeResizeEvent() {
    const bool resized = m_resized;
    m_resized = false;
    return resized;
}

} // namespace mc
