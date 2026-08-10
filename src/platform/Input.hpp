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

    f64 mouseDeltaX() const noexcept { return m_mouseDeltaX; }
    f64 mouseDeltaY() const noexcept { return m_mouseDeltaY; }

    /// Hides and grabs the cursor for mouse-look.
    void setCursorCaptured(bool captured);
    bool cursorCaptured() const noexcept { return m_cursorCaptured; }

private:
    static constexpr usize kKeyCount = static_cast<usize>(Key::Count);

    Window& m_window;

    bool m_current[kKeyCount] = {};
    bool m_previous[kKeyCount] = {};

    f64 m_mouseX = 0.0;
    f64 m_mouseY = 0.0;
    f64 m_mouseDeltaX = 0.0;
    f64 m_mouseDeltaY = 0.0;
    bool m_hasMouseSample = false;
    bool m_cursorCaptured = false;
};

} // namespace mc
