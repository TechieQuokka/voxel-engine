#include "world/ItemEntities.hpp"

#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "world/World.hpp"

#include <cmath>

namespace mc {
namespace {

/// Matches the player's gravity, so a dropped block falls at the rate the world
/// has already established rather than at a second, different one.
constexpr f32 kGravity = 28.0f;
constexpr f32 kTerminalVelocity = 40.0f;

/// How much horizontal speed a stack keeps per second while sliding. Items settle
/// quickly rather than skating, which is what stops a pile drifting off a ledge.
constexpr f32 kGroundDrag = 0.02f;
constexpr f32 kAirDrag = 0.72f;

/// Half-width of the item's collision box, in blocks. Small: an item should fall
/// into a one-block hole rather than bridging it.
constexpr f32 kHalfSize = 0.12f;

bool solidAt(const World& world, const vec3& position) {
    const BlockPos block{static_cast<i32>(std::floor(position.x)),
                         static_cast<i32>(std::floor(position.y)),
                         static_cast<i32>(std::floor(position.z))};
    // Solid, not merely non-air: a dropped block sinks through water rather than
    // bobbing on it. Vanilla floats items, which needs buoyancy this does not have.
    return isSolidBlock(world.blockAt(block));
}

f32 hashUnit(u32 value) {
    u32 h = value * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return static_cast<f32>(h >> 8) / 16777216.0f;
}

} // namespace

void ItemEntities::spawn(const vec3& position, BlockId block, u32 count) {
    if (block == kAirBlock || count == 0) {
        return;
    }

    const u32 seed = ++m_spawnCounter;

    ItemEntity item;
    item.position = position;
    item.block = block;
    item.count = count;
    item.pickupDelay = kPickupDelay;

    // A small outward pop, so breaking a wall does not leave every drop at exactly
    // the same point and merging into one indistinguishable stack.
    item.velocity = vec3{(hashUnit(seed * 3u + 0u) - 0.5f) * 1.6f,
                         1.4f + hashUnit(seed * 3u + 1u) * 0.8f,
                         (hashUnit(seed * 3u + 2u) - 0.5f) * 1.6f};

    m_items.push_back(item);
}

void ItemEntities::tick(const World& world, f32 dt) {
    MC_PROFILE_SCOPE_N("ItemEntities::tick");

    if (m_items.empty()) {
        return;
    }

    // Ageing runs on the real elapsed time; physics does not.
    for (ItemEntity& item : m_items) {
        item.age += dt;
        item.pickupDelay = std::max(0.0f, item.pickupDelay - dt);
    }

    // **Physics is substepped, and clamped.** The collision test asks whether the
    // destination is solid rather than sweeping the path to it, so a step longer
    // than a block tunnels straight through the floor -- and a frame after a stall,
    // or a window drag, delivers exactly that. Substepping keeps each move under a
    // block; the substep cap stops a long stall turning into a long catch-up, at the
    // honest cost of items lagging slightly behind real time after one.
    f32 remaining = std::min(dt, kMaxStep * static_cast<f32>(kMaxSubsteps));
    while (remaining > 0.0f) {
        const f32 step = std::min(remaining, kMaxStep);
        remaining -= step;
        integrate(world, step);
    }

    merge();
    cull(world);
}

void ItemEntities::integrate(const World& world, f32 dt) {
    for (ItemEntity& item : m_items) {
        item.velocity.y = std::max(item.velocity.y - kGravity * dt, -kTerminalVelocity);

        // Axis at a time, so sliding along a wall works and a corner does not stop
        // the item dead. The same "accept or refuse whole" rule walking uses -- and
        // the same limitation: no swept test, so a fast item can pass a thin wall.
        // Items never move fast enough for that to matter, which is why it is fine
        // here and would not be for a projectile.
        const vec3 step = item.velocity * dt;

        vec3 next = item.position;
        next.x += step.x;
        if (solidAt(world, vec3{next.x + (step.x > 0.0f ? kHalfSize : -kHalfSize),
                                next.y, next.z})) {
            next.x = item.position.x;
            item.velocity.x = 0.0f;
        }

        next.z += step.z;
        if (solidAt(world, vec3{next.x, next.y,
                                next.z + (step.z > 0.0f ? kHalfSize : -kHalfSize)})) {
            next.z = item.position.z;
            item.velocity.z = 0.0f;
        }

        next.y += step.y;
        item.onGround = false;
        if (solidAt(world, vec3{next.x, next.y - kHalfSize, next.z})) {
            // Land on top of whatever was hit rather than inside it.
            next.y = std::floor(next.y - kHalfSize) + 1.0f + kHalfSize;
            item.velocity.y = 0.0f;
            item.onGround = true;
        } else if (step.y > 0.0f && solidAt(world, vec3{next.x, next.y + kHalfSize, next.z})) {
            next.y = item.position.y;
            item.velocity.y = 0.0f;
        }

        item.position = next;

        const f32 drag = item.onGround ? kGroundDrag : kAirDrag;
        const f32 keep = std::pow(drag, dt);
        item.velocity.x *= keep;
        item.velocity.z *= keep;
    }
}

void ItemEntities::merge() {
    // Merge touching stacks of the same block. Quadratic, and deliberately so: the
    // list is a few dozen entries, and a spatial index for that is more code than
    // the thing it accelerates.
    for (usize i = 0; i < m_items.size(); ++i) {
        if (m_items[i].count == 0) {
            continue;
        }
        for (usize j = i + 1; j < m_items.size(); ++j) {
            if (m_items[j].count == 0 || m_items[j].block != m_items[i].block) {
                continue;
            }
            if (m_items[i].count + m_items[j].count > kMaxStack) {
                continue;
            }
            const vec3 delta = m_items[i].position - m_items[j].position;
            if (math::dot(delta, delta) > kMergeDistance * kMergeDistance) {
                continue;
            }
            m_items[i].count += m_items[j].count;
            // The survivor keeps the younger age, so merging does not shorten a
            // fresh stack's life to that of the one it absorbed.
            m_items[i].age = std::min(m_items[i].age, m_items[j].age);
            m_items[j].count = 0;
        }
    }
}

void ItemEntities::cull(const World& world) {
    std::erase_if(m_items, [&](const ItemEntity& item) {
        if (item.count == 0 || item.age >= kDespawnSeconds) {
            return true;
        }
        if (!isValidWorldY(static_cast<i32>(std::floor(item.position.y)))) {
            return true; // Fell out of the world.
        }
        // Its column unloaded. This is what a per-chunk store would get for free,
        // and it is the whole reason a flat list is affordable.
        return world.find(toChunkPos(BlockPos{static_cast<i32>(std::floor(item.position.x)),
                                              0,
                                              static_cast<i32>(std::floor(item.position.z))}))
               == nullptr;
    });
}

f32 ItemEntities::distanceSquaredTo(const PickupVolume& volume, const vec3& point) {
    // Clamp onto the segment before measuring. An item anywhere between the feet and
    // the top of the head is at zero vertical distance, which is what makes standing
    // on one work -- the case the eye-relative sphere could never reach.
    const f32 clampedY =
        std::clamp(point.y, volume.feet.y, volume.feet.y + volume.height);
    const vec3 delta{point.x - volume.feet.x, point.y - clampedY,
                     point.z - volume.feet.z};
    return math::dot(delta, delta);
}

std::vector<ItemEntities::Pickup> ItemEntities::collect(const PickupVolume& volume) {
    std::vector<Pickup> taken;
    if (m_items.empty()) {
        return taken;
    }

    const f32 radiusSquared = volume.radius * volume.radius;

    for (ItemEntity& item : m_items) {
        if (item.pickupDelay > 0.0f) {
            continue;
        }
        if (distanceSquaredTo(volume, item.position) > radiusSquared) {
            continue;
        }
        taken.push_back(Pickup{item.block, item.count});
        item.count = 0;
    }

    if (!taken.empty()) {
        std::erase_if(m_items, [](const ItemEntity& item) { return item.count == 0; });
    }
    return taken;
}

} // namespace mc
