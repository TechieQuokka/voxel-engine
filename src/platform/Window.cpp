#include "platform/Window.hpp"

#include "core/Log.hpp"

#include <GLFW/glfw3.h>

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

    setVsync(config.vsync);

    logInfo("Window created: {}x{} (framebuffer {}x{})",
            config.width, config.height, m_framebufferWidth, m_framebufferHeight);
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
    glfwSwapInterval(enabled ? 1 : 0);
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
