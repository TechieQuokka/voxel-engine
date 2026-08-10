#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <array>

namespace mc {

/// View frustum as a set of inward-facing planes, for AABB culling.
///
/// **Five planes, not six.** The projection is infinite reversed-Z (see Camera):
/// depth approaches 0 asymptotically instead of reaching a far plane, so the row of
/// the matrix that would give the far plane is `(0, 0, 0, near)` — a zero normal.
/// The textbook Gribb-Hartmann extraction produces that plane and then normalizes
/// it, which divides by zero and yields a plane that rejects everything. There is
/// simply nothing beyond which geometry is too distant to draw, which is the whole
/// point of the infinite projection.
///
/// Planes are stored as `vec4(normal, distance)` with the normal normalized and
/// pointing into the frustum, so a point is inside when `dot(n, p) + d >= 0`.
class Frustum {
public:
    enum Plane : usize {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Count,
    };

    static constexpr usize kPlaneCount = static_cast<usize>(Count);

    Frustum() = default;

    /// `viewProjection` must be the same matrix the vertex shader uses, or culling
    /// will disagree with rasterization at the edges.
    explicit Frustum(const mat4& viewProjection);

    /// Conservative: never reports a visible box as hidden, but may report a hidden
    /// box as visible for boxes just outside a frustum corner. That is the correct
    /// bias — a false positive costs one wasted draw, a false negative is a hole in
    /// the world.
    bool intersectsAabb(const vec3& minCorner, const vec3& maxCorner) const;

    const vec4& plane(usize index) const { return m_planes[index]; }

private:
    std::array<vec4, kPlaneCount> m_planes{};
};

/// World-space bounds of one 32³ section.
inline void sectionBounds(SectionPos pos, vec3& minCorner, vec3& maxCorner) {
    const auto size = static_cast<f32>(kSectionSize);
    minCorner = vec3{static_cast<f32>(pos.x) * size,
                     static_cast<f32>(pos.y) * size,
                     static_cast<f32>(pos.z) * size};
    maxCorner = minCorner + vec3{size, size, size};
}

/// World-space bounds of a whole column, all 12 sections of it.
///
/// Tested before the sections it contains: one plane test against a 32x384x32 box
/// rejects twelve section tests, and at distance 16 the columns outside the frustum
/// are the large majority.
inline void columnBounds(ChunkPos pos, vec3& minCorner, vec3& maxCorner) {
    const auto size = static_cast<f32>(kSectionSize);
    minCorner = vec3{static_cast<f32>(pos.x) * size,
                     static_cast<f32>(kWorldMinY),
                     static_cast<f32>(pos.z) * size};
    maxCorner = vec3{minCorner.x + size, static_cast<f32>(kWorldMaxY), minCorner.z + size};
}

} // namespace mc
