#include "render/Camera.hpp"

#include <cmath>

namespace mc {
namespace {

/// Just under pi/2. At exactly pi/2 the forward vector becomes parallel to the
/// world up axis and the view basis degenerates.
constexpr f32 kMaxPitch = 1.5533f;

} // namespace

void Camera::setOrientation(f32 yaw, f32 pitch) {
    m_yaw = yaw;
    m_pitch = math::clamp(pitch, -kMaxPitch, kMaxPitch);
}

void Camera::setPerspective(f32 fovYRadians, f32 aspect, f32 nearPlane) {
    // Infinite reversed-Z projection. There is no far plane: depth approaches 0
    // asymptotically instead of clipping, which removes far-plane tuning from
    // the render distance problem entirely.
    const f32 focal = 1.0f / std::tan(fovYRadians * 0.5f);

    m_projection = mat4(0.0f);
    m_projection[0][0] = focal / aspect;
    m_projection[1][1] = focal;
    m_projection[2][3] = -1.0f;
    m_projection[3][2] = nearPlane;
}

vec3 Camera::forward() const {
    const f32 cosPitch = std::cos(m_pitch);
    return {cosPitch * std::sin(m_yaw), std::sin(m_pitch), -cosPitch * std::cos(m_yaw)};
}

vec3 Camera::right() const {
    return math::normalize(math::cross(forward(), up()));
}

mat4 Camera::viewMatrix() const {
    return math::lookAt(m_position, m_position + forward(), up());
}

} // namespace mc
