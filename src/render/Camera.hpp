#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"

namespace mc {

/// Free-flying first-person camera.
///
/// Uses a reversed-Z infinite projection: near and far are swapped and the far
/// plane is at infinity. Floating-point precision is densest near 0, and with
/// reversed Z that density lands where the depth values are far away -- which
/// is exactly where a 1024-block render distance would otherwise z-fight. The
/// depth buffer must be cleared to 0 and compared with GREATER.
class Camera {
public:
    void setPosition(const vec3& position) { m_position = position; }
    const vec3& position() const noexcept { return m_position; }

    /// Radians. Pitch is clamped to just under +/- 90 degrees.
    void setOrientation(f32 yaw, f32 pitch);
    f32 yaw() const noexcept { return m_yaw; }
    f32 pitch() const noexcept { return m_pitch; }

    void setPerspective(f32 fovYRadians, f32 aspect, f32 nearPlane);

    void move(const vec3& worldDelta) { m_position += worldDelta; }
    void rotate(f32 deltaYaw, f32 deltaPitch) { setOrientation(m_yaw + deltaYaw, m_pitch + deltaPitch); }

    vec3 forward() const;
    vec3 right() const;
    static vec3 up() { return {0.0f, 1.0f, 0.0f}; }

    mat4 viewMatrix() const;
    const mat4& projectionMatrix() const noexcept { return m_projection; }
    mat4 viewProjectionMatrix() const { return m_projection * viewMatrix(); }

private:
    vec3 m_position{0.0f, 0.0f, 0.0f};
    f32 m_yaw = 0.0f;
    f32 m_pitch = 0.0f;

    mat4 m_projection{1.0f};
};

} // namespace mc
