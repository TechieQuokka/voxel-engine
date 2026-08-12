#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <algorithm>
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

    /// The volume a player picks items up into: a vertical segment from the feet to
    /// the top of the head, with a radius around it.
    ///
    /// **A segment rather than a point, and the reason is a bug that shipped in 7.10
    /// and survived four play sessions.** Pickup used to be a sphere measured from
    /// `Camera::position()`, which is the *eye*. An item comes to rest at ground +
    /// `kHalfSize` (0.12) and the eye sits at ground + `kEyeHeight` (1.62), so
    /// standing directly on top of an item put it 1.50 away against a radius of
    /// 1.4 -- out of range while standing on it, on flat ground, always. The whole
    /// break-drop-collect-place loop had one step that had never worked.
    ///
    /// Vanilla measures from the player's bounding box, which is exactly why it does
    /// not care where the eye is. This is the same answer without a collider: clamp
    /// onto the body's vertical extent first, then measure. `placeTargetBlock` uses
    /// this shape of test for "am I standing here", and both would be better served
    /// by the real collider HANDOFF.md 8 wants.
    struct PickupVolume {
        /// The bottom of the player. **Not the eye and not the camera** -- passing
        /// either is the bug above.
        vec3 feet{};
        /// Feet to the top of the head.
        f32 height = 0.0f;
        /// Reach measured from the body axis, not from the eye.
        f32 radius = 0.0f;
    };

    /// Squared distance from `point` to the volume's axis segment.
    ///
    /// Public because pinning this geometry against the player's real dimensions is
    /// the point of it -- see the Engine-geometry cases in `test_items.cpp`. Nothing
    /// else about the failure above was testable from inside this class: both
    /// constants that made it wrong lived in the caller.
    static f32 distanceSquaredTo(const PickupVolume& volume, const vec3& point);

    /// Removes and accumulates everything inside `volume` that is past its pickup
    /// delay. Returns the picked-up stacks, which the caller adds to an inventory.
    struct Pickup {
        BlockId block = kAirBlock;
        u32 count = 0;
    };
    std::vector<Pickup> collect(const PickupVolume& volume);

    /// Offers everything in range to `accept`, which takes what it can and returns
    /// how many it could not. Anything left over stays on the ground.
    ///
    /// **This exists because stack limits made a full inventory possible.** The
    /// simple form above removes an item whether or not the caller had room for it,
    /// which was fine when the inventory was an unbounded count per block type and
    /// silently deletes a stack now that it is 36 slots of 64. A player walking over
    /// a diamond with a full pack should see it stay there, which is also what
    /// vanilla does.
    template <typename F>
    void collectInto(const PickupVolume& volume, F&& accept) {
        if (m_items.empty()) {
            return;
        }
        const f32 radiusSquared = volume.radius * volume.radius;

        for (ItemEntity& item : m_items) {
            if (item.pickupDelay > 0.0f || item.count == 0) {
                continue;
            }
            if (distanceSquaredTo(volume, item.position) > radiusSquared) {
                continue;
            }
            const u32 leftover = accept(item.block, item.count);
            // Clamped rather than trusted: an acceptor that returned more than it was
            // offered would otherwise create items out of nothing.
            item.count = std::min(leftover, item.count);
        }

        std::erase_if(m_items, [](const ItemEntity& item) { return item.count == 0; });
    }

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
    /// Reach for `PickupVolume::radius`. Vanilla inflates the player's bounding box
    /// by one block horizontally, which is a block from the item plus the player's
    /// own half-width.
    ///
    /// **It lives here rather than in the Engine, which is where it used to be.**
    /// The failure documented on `PickupVolume` was a relationship between this
    /// number and the player's dimensions, and it was untestable while this was a
    /// private constant in the caller.
    static constexpr f32 kPickupRadius = 1.4f;
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
