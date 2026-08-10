#include "render/Frustum.hpp"

#include "core/Assert.hpp"

#include <cmath>

namespace mc {
namespace {

/// Row `index` of a GLM matrix. GLM is column-major, so `m[col][row]` -- a
/// mathematical row has to be gathered across the columns, and getting this
/// backwards produces a frustum that looks plausible and culls the wrong things.
vec4 row(const mat4& m, int index) {
    return vec4{m[0][index], m[1][index], m[2][index], m[3][index]};
}

vec4 normalizedPlane(const vec4& plane) {
    const f32 length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    MC_ASSERT_MSG(length > 0.0f, "degenerate frustum plane -- is the far plane being extracted?");
    return plane / length;
}

} // namespace

Frustum::Frustum(const mat4& viewProjection) {
    const vec4 r0 = row(viewProjection, 0);
    const vec4 r1 = row(viewProjection, 1);
    const vec4 r2 = row(viewProjection, 2);
    const vec4 r3 = row(viewProjection, 3);

    // Clip-space constraints, with glClipControl(ZERO_TO_ONE):
    //   -w <= x <= w,  -w <= y <= w,  0 <= z <= w
    //
    // Reversed-Z puts the near plane at depth 1, so `z <= w` is the *near* plane
    // and `z >= 0` would be the far one -- which is the row that degenerates.
    m_planes[Left] = normalizedPlane(r3 + r0);
    m_planes[Right] = normalizedPlane(r3 - r0);
    m_planes[Bottom] = normalizedPlane(r3 + r1);
    m_planes[Top] = normalizedPlane(r3 - r1);
    m_planes[Near] = normalizedPlane(r3 - r2);
}

bool Frustum::intersectsAabb(const vec3& minCorner, const vec3& maxCorner) const {
    for (const vec4& plane : m_planes) {
        // The box's furthest corner along the plane normal. If even that one is
        // behind the plane, every corner is, and the box is outside.
        const vec3 furthest{plane.x >= 0.0f ? maxCorner.x : minCorner.x,
                            plane.y >= 0.0f ? maxCorner.y : minCorner.y,
                            plane.z >= 0.0f ? maxCorner.z : minCorner.z};

        if (plane.x * furthest.x + plane.y * furthest.y + plane.z * furthest.z + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace mc
