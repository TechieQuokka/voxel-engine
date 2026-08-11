#include "platform/Input.hpp"

#include "platform/Window.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>

namespace mc {
namespace {

// Indexed by Key; order must match the enum.
constexpr std::array<int, static_cast<usize>(Key::Count)> kGlfwKeys{{
    GLFW_KEY_W,
    GLFW_KEY_A,
    GLFW_KEY_S,
    GLFW_KEY_D,
    GLFW_KEY_SPACE,
    GLFW_KEY_LEFT_SHIFT,
    GLFW_KEY_LEFT_CONTROL,
    GLFW_KEY_ESCAPE,
    GLFW_KEY_F,
    GLFW_KEY_F1,
    GLFW_KEY_F3,
    GLFW_KEY_F5,
    GLFW_KEY_1,
    GLFW_KEY_2,
    GLFW_KEY_3,
    GLFW_KEY_4,
    GLFW_KEY_5,
    GLFW_KEY_6,
    GLFW_KEY_7,
    GLFW_KEY_8,
    GLFW_KEY_9,
}};

// Indexed by MouseButton; order must match the enum.
constexpr std::array<int, static_cast<usize>(MouseButton::Count)> kGlfwButtons{{
    GLFW_MOUSE_BUTTON_LEFT,
    GLFW_MOUSE_BUTTON_RIGHT,
    GLFW_MOUSE_BUTTON_MIDDLE,
}};

GLFWwindow* handleOf(const Window& window) {
    return static_cast<GLFWwindow*>(window.nativeHandle());
}

} // namespace

Input::Input(Window& window) : m_window(window) {}

void Input::update() {
    GLFWwindow* handle = handleOf(m_window);

    std::copy(std::begin(m_current), std::end(m_current), std::begin(m_previous));
    for (usize i = 0; i < kKeyCount; ++i) {
        m_current[i] = glfwGetKey(handle, kGlfwKeys[i]) == GLFW_PRESS;
    }

    std::copy(std::begin(m_currentButtons), std::end(m_currentButtons),
              std::begin(m_previousButtons));
    for (usize i = 0; i < kButtonCount; ++i) {
        m_currentButtons[i] = glfwGetMouseButton(handle, kGlfwButtons[i]) == GLFW_PRESS;
    }

    f64 x = 0.0;
    f64 y = 0.0;
    glfwGetCursorPos(handle, &x, &y);

    if (m_hasMouseSample) {
        m_mouseDeltaX = x - m_mouseX;
        m_mouseDeltaY = y - m_mouseY;
    } else {
        // The first sample after capture would otherwise produce a huge delta
        // and snap the camera.
        m_mouseDeltaX = 0.0;
        m_mouseDeltaY = 0.0;
        m_hasMouseSample = true;
    }
    m_mouseX = x;
    m_mouseY = y;
}

bool Input::isDown(Key key) const {
    return m_current[static_cast<usize>(key)];
}

bool Input::wasPressed(Key key) const {
    const auto index = static_cast<usize>(key);
    return m_current[index] && !m_previous[index];
}

bool Input::isDown(MouseButton button) const {
    return m_currentButtons[static_cast<usize>(button)];
}

bool Input::wasPressed(MouseButton button) const {
    const auto index = static_cast<usize>(button);
    return m_currentButtons[index] && !m_previousButtons[index];
}

void Input::setCursorCaptured(bool captured) {
    if (captured == m_cursorCaptured) {
        return;
    }
    m_cursorCaptured = captured;
    m_hasMouseSample = false; // Force a fresh baseline on the next update.

    glfwSetInputMode(handleOf(m_window), GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

} // namespace mc
