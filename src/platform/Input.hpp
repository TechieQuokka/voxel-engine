#pragma once

#include "core/Types.hpp"

namespace mc {

class Window;

/// Keys the engine currently reacts to. Deliberately an engine-level enum
/// rather than GLFW's codes, so key constants do not leak the windowing
/// backend into every caller.
enum class Key : u32 {
    W, A, S, D,
    Space, LeftShift, LeftControl,
    Escape,
    F, F1, F3, F5,
    /// The hotbar. Contiguous and in order, so a slot is `Num1 + n` -- the block
    /// selector indexes them arithmetically rather than through a switch.
    Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Count,
};

/// Mouse buttons the engine reacts to. Separate from Key because GLFW numbers them
/// in their own space, and because "was this pressed" for a button is asked at a
/// different place in the frame than for a key.
enum class MouseButton : u32 {
    Left,
    Right,
    Middle,
    Count,
};

/// Per-frame keyboard and mouse state.
///
/// Polled rather than callback-driven: the frame loop wants a coherent snapshot
/// of input taken at one point in time, not events arriving mid-update.
class Input {
public:
    explicit Input(Window& window);

    /// Samples the current state. Call once per frame, after pollEvents().
    void update();

    bool isDown(Key key) const;
    /// True only on the frame the key transitioned from up to down.
    bool wasPressed(Key key) const;

    bool isDown(MouseButton button) const;
    /// True only on the frame the button transitioned from up to down.
    ///
    /// Breaking and placing are both edge-triggered on purpose. Holding a button
    /// down at 60 FPS would place sixty blocks a second along the view ray, which is
    /// not "building" -- Minecraft repeats on a timer for breaking and not at all
    /// for placing, and a timer is not worth having before there is a block that
    /// takes more than one hit.
    bool wasPressed(MouseButton button) const;

    f64 mouseDeltaX() const noexcept { return m_mouseDeltaX; }
    f64 mouseDeltaY() const noexcept { return m_mouseDeltaY; }

    /// Hides and grabs the cursor for mouse-look.
    void setCursorCaptured(bool captured);
    bool cursorCaptured() const noexcept { return m_cursorCaptured; }

private:
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);
    static constexpr usize kButtonCount = static_cast<usize>(MouseButton::Count);

    Window& m_window;

    bool m_current[kKeyCount] = {};
    bool m_previous[kKeyCount] = {};

    bool m_currentButtons[kButtonCount] = {};
    bool m_previousButtons[kButtonCount] = {};

    f64 m_mouseX = 0.0;
    f64 m_mouseY = 0.0;
    f64 m_mouseDeltaX = 0.0;
    f64 m_mouseDeltaY = 0.0;
    bool m_hasMouseSample = false;
    bool m_cursorCaptured = false;
};

} // namespace mc
