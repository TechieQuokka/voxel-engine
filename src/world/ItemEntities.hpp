#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <vector>

namespace mc {

class World;

/// A block dropped on the ground, waiting to be picked up.
///
/// **The first thing in this engine that exists in the world and is not a voxel.**
/// Everything until now was either terrain or drawn at the player's position; this
/// has a position of its own, moves under gravity, and outlives the frame that made
/// it.
struct ItemEntity {
    vec3 position{};
    vec3 velocity{};

    /// What it is. Items are block types here -- breaking oak_log gives an oak_log.
    /// That will not survive crafting, which needs sticks and tools that are not
    /// blocks, and the split is deliberately deferred until something needs it.
    BlockId block = kAirBlock;
    /// How many. Stacks merge on contact, which is what keeps the entity count
    /// bounded when a player tears through a hillside.
    u32 count = 1;

    /// Seconds since it was dropped. Drives both the despawn timer and the bob.
    f32 age = 0.0f;
    /// Counts down before the item may be picked up, so a block does not fly
    /// straight back into the hand that broke it.
    f32 pickupDelay = 0.0f;

    bool onGround = false;
};

/// Every dropped item in the loaded world.
///
/// A flat list rather than per-chunk storage. Minecraft keeps entities in the chunk
/// they occupy, which is the right answer once they have to be saved and ticked
/// selectively -- but it would mean threading entity lifetime through the pin and
/// unload machinery for a few dozen objects. A flat list with an explicit "drop
/// anything whose column went away" sweep gets the same behaviour, and the day this
/// holds thousands of entities is the day to move it.
class ItemEntities {
public:
    /// Drops `count` of `block` at `position`, with a small random-ish scatter so a
    /// pile does not stack into one point.
    void spawn(const vec3& position, BlockId block, u32 count);

    /// Advances physics, ages everything, merges touching stacks, and removes what
    /// despawned or fell out of the loaded world.
    void tick(const World& world, f32 dt);

    /// Removes and accumulates everything within `radius` of `position` that is
    /// past its pickup delay. Returns the picked-up stacks, which the caller adds
    /// to an inventory.
    struct Pickup {
        BlockId block = kAirBlock;
        u32 count = 0;
    };
    std::vector<Pickup> collect(const vec3& position, f32 radius);

    const std::vector<ItemEntity>& items() const noexcept { return m_items; }
    usize size() const noexcept { return m_items.size(); }
    void clear() { m_items.clear(); }

    /// Vanilla despawns dropped items after five minutes. Without it a long session
    /// in a mine grows the list without bound.
    static constexpr f32 kDespawnSeconds = 300.0f;
    /// Half a second, matching vanilla's ten ticks.
    static constexpr f32 kPickupDelay = 0.5f;
    /// How close two stacks of the same block have to be to become one.
    static constexpr f32 kMergeDistance = 0.6f;
    /// Beyond a full stack they stop merging, which is what stops one entity
    /// silently accumulating an entire mining session.
    static constexpr u32 kMaxStack = 64;

    /// Longest physics step taken, and how many of them one `tick` may run.
    ///
    /// The collision test asks whether the destination is solid rather than sweeping
    /// the path to it, so a step that moves more than a block tunnels through the
    /// floor. A frame after a stall delivers exactly that, and so does a debugger
    /// breakpoint. The cap on substeps stops a long stall becoming a long catch-up.
    static constexpr f32 kMaxStep = 1.0f / 60.0f;
    static constexpr u32 kMaxSubsteps = 4;

private:
    void integrate(const World& world, f32 dt);
    void merge();
    void cull(const World& world);

    std::vector<ItemEntity> m_items;
    /// Seed for the scatter. Not thread-safe and does not need to be: spawning
    /// happens on the main thread, from a click.
    u32 m_spawnCounter = 0;
};

} // namespace mc
