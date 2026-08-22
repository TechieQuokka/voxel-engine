#include "world/Raycast.hpp"

#include "world/BlockShape.hpp"
#include "world/BlockTable.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

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

/// Where a ray enters one box, and through which face.
struct BoxEntry {
    f32 distance = 0.0f;
    Face face = Face::PosY;
};

/// Ray against one box of a block, in world coordinates.
///
/// **The slab method**, which is the standard ray-AABB test: clip the ray against each
/// axis's pair of planes and keep the latest entry and the earliest exit. They cross
/// only if the ray misses. The axis that produced the latest entry is the face the ray
/// came in through, which is the same thing the DDA gets for free from a cell boundary
/// -- here it has to be worked out, because the surface is inside the cell.
std::optional<BoxEntry> enterBox(BlockPos cell, const BlockBox& box, vec3 origin,
                                 vec3 direction) {
    const f32 cellMin[3] = {static_cast<f32>(cell.x), static_cast<f32>(cell.y),
                            static_cast<f32>(cell.z)};
    const f32 low[3] = {cellMin[0] + box.lowX(), cellMin[1] + box.lowY(),
                        cellMin[2] + box.lowZ()};
    const f32 high[3] = {cellMin[0] + box.highX(), cellMin[1] + box.highY(),
                         cellMin[2] + box.highZ()};
    const f32 from[3] = {origin.x, origin.y, origin.z};
    const f32 along[3] = {direction.x, direction.y, direction.z};

    f32 near = -std::numeric_limits<f32>::infinity();
    f32 far = std::numeric_limits<f32>::infinity();
    i32 nearAxis = -1;

    for (i32 axis = 0; axis < 3; ++axis) {
        if (along[axis] == 0.0f) {
            // Parallel to this pair of planes: a miss unless already between them.
            // Spelled out rather than left to infinities, which would produce a NaN
            // here rather than a decision.
            if (from[axis] < low[axis] || from[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }

        const f32 inverse = 1.0f / along[axis];
        f32 enter = (low[axis] - from[axis]) * inverse;
        f32 leave = (high[axis] - from[axis]) * inverse;
        if (enter > leave) {
            std::swap(enter, leave);
        }
        if (enter > near) {
            near = enter;
            nearAxis = axis;
        }
        far = std::min(far, leave);
        if (near > far) {
            return std::nullopt;
        }
    }

    if (far < 0.0f || nearAxis < 0) {
        return std::nullopt;
    }

    // The face's outward normal points back the way the ray came, which is what makes
    // `adjacent` the cell a placement belongs in.
    static constexpr Face kNegative[3] = {Face::PosX, Face::PosY, Face::PosZ};
    static constexpr Face kPositive[3] = {Face::NegX, Face::NegY, Face::NegZ};
    const Face face = along[nearAxis] > 0.0f ? kPositive[nearAxis] : kNegative[nearAxis];

    return BoxEntry{std::max(near, 0.0f), face};
}

/// The nearest box of `block` the ray enters within this cell, if any.
///
/// **This is what a non-cube block costs the aim ray, and leaving it out is what made
/// slabs unbuildable.** The DDA below decides a hit from the cell alone; a slab fills
/// half of one, so a ray through the empty half was reported as hitting it -- and the
/// face came from the cell boundary, so `adjacent` named a cell the player was not
/// pointing at. Placed blocks landed one cell to the side, over and over.
std::optional<BoxEntry> enterBlock(BlockPos cell, BlockId block, vec3 origin,
                                   vec3 direction) {
    std::optional<BoxEntry> best;
    for (const BlockBox& box : blockBoxes(block)) {
        const std::optional<BoxEntry> entry = enterBox(cell, box, origin, direction);
        if (entry.has_value() && (!best.has_value() || entry->distance < best->distance)) {
            best = entry;
        }
    }
    return best;
}

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
        const BlockId id = world.blockAt(block);
        // Fluids are not targets. Vanilla needs a bucket to remove water and this
        // engine has no items at all, so a crosshair over the sea should find the
        // sea bed rather than a surface nothing can do anything with.
        if (!isSolidBlock(id)) {
            continue;
        }

        // **A block that does not fill its cell is tested against its own boxes.** The
        // cell boundary the DDA just crossed is the right face and the right distance
        // only when the surface *is* that boundary. For a slab the ray may pass
        // through the empty half and go on, and when it does hit, the face is one of
        // the box's own -- the top of a bottom slab is a `PosY` face in the middle of
        // a cell, which no boundary crossing can report.
        if (!isFullCube(id)) {
            const std::optional<BoxEntry> entry = enterBlock(block, id, origin, direction);
            if (!entry.has_value() || entry->distance > maxDistance) {
                continue; // Through the empty part of the cell: keep marching.
            }
            return RaycastHit{block, offsetByFace(block, entry->face), entry->face,
                              entry->distance};
        }

        // The face is the one the ray came *in* through, so its outward normal
        // points back the way the ray came -- which is exactly the empty cell a
        // placement belongs in.
        return RaycastHit{block, offsetByFace(block, face), face, travelled};
    }

    return std::nullopt;
}

} // namespace mc
