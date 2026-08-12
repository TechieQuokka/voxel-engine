#include "world/Raycast.hpp"

#include "world/BlockTable.hpp"
#include "world/World.hpp"

#include <cmath>
#include <limits>

namespace mc {
namespace {

/// Distance along the ray to the first grid line crossed on one axis, and the
/// distance between successive crossings on it.
struct Axis {
    /// +1 or -1. Zero-length components get +1 and an infinite `next`, so the axis
    /// is simply never chosen.
    i32 step = 1;
    f32 next = std::numeric_limits<f32>::infinity();
    f32 delta = std::numeric_limits<f32>::infinity();
};

Axis setUpAxis(f32 origin, f32 direction, i32 cell) {
    Axis axis;
    if (direction == 0.0f) {
        return axis; // Parallel to this axis: it is never the nearest boundary.
    }

    axis.step = direction > 0.0f ? 1 : -1;
    axis.delta = std::abs(1.0f / direction);

    // Distance to the boundary being approached: the far side of the cell going
    // positive, the near side going negative.
    const f32 boundary = static_cast<f32>(direction > 0.0f ? cell + 1 : cell);
    axis.next = (boundary - origin) / direction;
    return axis;
}

} // namespace

std::optional<RaycastHit> raycast(const World& world, vec3 origin, vec3 direction,
                                  f32 maxDistance) {
    const f32 length = math::length(direction);
    if (!(length > 0.0f) || !(maxDistance > 0.0f)) {
        return std::nullopt;
    }
    direction /= length;

    i32 x = static_cast<i32>(std::floor(origin.x));
    i32 y = static_cast<i32>(std::floor(origin.y));
    i32 z = static_cast<i32>(std::floor(origin.z));

    // The eye can legitimately be inside a solid block -- standing in tall terrain
    // that just streamed in, or the moment after placing one. Reporting a hit at
    // distance zero would let a click break whatever the head is buried in, so the
    // starting cell is skipped and the march begins at the first boundary.
    Axis ax = setUpAxis(origin.x, direction.x, x);
    Axis ay = setUpAxis(origin.y, direction.y, y);
    Axis az = setUpAxis(origin.z, direction.z, z);

    f32 travelled = 0.0f;
    while (travelled <= maxDistance) {
        // Cross whichever boundary is nearest. The axis chosen is also the axis the
        // face lies on, which is where `face` comes from -- no second test needed.
        Face face = Face::PosX;
        if (ax.next <= ay.next && ax.next <= az.next) {
            travelled = ax.next;
            x += ax.step;
            ax.next += ax.delta;
            face = ax.step > 0 ? Face::NegX : Face::PosX;
        } else if (ay.next <= az.next) {
            travelled = ay.next;
            y += ay.step;
            ay.next += ay.delta;
            face = ay.step > 0 ? Face::NegY : Face::PosY;
        } else {
            travelled = az.next;
            z += az.step;
            az.next += az.delta;
            face = az.step > 0 ? Face::NegZ : Face::PosZ;
        }

        if (travelled > maxDistance) {
            break;
        }
        if (!isValidWorldY(y)) {
            // Above the sky the ray may still come back down, so only a ray heading
            // further out is finished. Below the world it can only continue down.
            if (y >= kWorldMaxY && direction.y >= 0.0f) {
                break;
            }
            if (y < kWorldMinY && direction.y <= 0.0f) {
                break;
            }
            continue;
        }

        const BlockPos block{x, y, z};
        // Fluids are not targets. Vanilla needs a bucket to remove water and this
        // engine has no items at all, so a crosshair over the sea should find the
        // sea bed rather than a surface nothing can do anything with.
        if (!isSolidBlock(world.blockAt(block))) {
            continue;
        }

        // The face is the one the ray came *in* through, so its outward normal
        // points back the way the ray came -- which is exactly the empty cell a
        // placement belongs in.
        return RaycastHit{block, offsetByFace(block, face), face, travelled};
    }

    return std::nullopt;
}

} // namespace mc
