#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"

namespace mc {

/// How a walking box moves horizontally: one axis at a time, stepping up what it
/// can reach and refusing what it cannot.
///
/// **Extracted from `Engine::updateWalk` so it can be tested at all.** It was the
/// densest set of rules in the engine's largest file, every one of them found by
/// playing rather than by reasoning -- axis separation because a player who walks
/// into a wall at an angle should slide along it, the wedge escape because terrain
/// can arrive around a player and refusing motion then traps them with no way out
/// but quitting, and step-up refused in mid-air because stepping up while falling
/// is climbing. None of that was reachable from a test while it lived inside a
/// method that needed a window, a device and a streaming world to call.
///
/// It stays a template on the blocking test rather than taking a `World`: the caller
/// already knows how to ask its own question -- `Engine` asks about solid blocks and
/// walks the player box over them -- and a header in `world` that needed `World`
/// would be the same coupling this file exists to avoid.
struct WalkMove {
    /// Vanilla's step height, and the reason a full block has to be jumped.
    ///
    /// This was 1.05 in this engine and it felt wrong, because it *was* wrong:
    /// Minecraft steps up at most 0.6 of a block without jumping, which covers slabs
    /// and stairs and nothing else. Stepping a whole block automatically is a mod,
    /// not the game. Since every block in this world is full height, 0.6 means no
    /// rise is ever walked up and every one of them is a jump -- which is exactly
    /// how the real thing feels.
    static constexpr f32 kStepHeight = 0.6f;

    /// How finely the step-up is probed. Six tries between 0 and `kStepHeight`,
    /// which is enough to find the top of any surface on the block lattice and cheap
    /// enough that it does not matter that it is a search rather than a solve.
    static constexpr f32 kStepProbe = 0.1f;

    /// How much a sneaking move is shortened by at a time when it would leave the
    /// ledge. Vanilla's 0.05.
    ///
    /// **Shortening rather than refusing is the whole feature.** A move that is simply
    /// denied stops the player a whole step back from the edge, which is exactly where
    /// you cannot stand to build outward -- and building outward from a ledge is what
    /// sneaking is *for*. Creeping up in twentieths of a block puts the box's edge on
    /// the block's edge, which is what makes the next block placeable.
    static constexpr f32 kEdgeBackOff = 0.05f;
};

/// Moves `feet` by `dx` and `dz`, and returns where it ends up.
///
/// `blocked(feet)` answers whether the player's box at those feet overlaps anything
/// that stops it. `onGround` gates the step-up: stepping up in mid-air is climbing.
///
/// **The axes are separated on purpose.** Testing the diagonal as one move would
/// stop a player dead when they walk into a wall at an angle; refusing X while Z
/// still goes through is what makes them slide along it. It is also what stops a
/// corner being cut diagonally through the block that forms it.
///
/// **A box that is already inside something may always move.** Terrain streams in
/// around a standing player and a falling block can land on one, and a move refused
/// then would wedge them in place permanently. The escape is deliberately generous:
/// any motion at all is better than none, because none is unrecoverable.
template <typename BlockedFn>
vec3 slideWithStepUp(vec3 feet, f32 dx, f32 dz, bool onGround, BlockedFn blocked,
                     bool sneaking = false) {
    // Asked once, before either axis, so that a move which begins wedged stays
    // permitted for its whole length rather than being re-judged half way through by
    // a position the first axis just created.
    const bool wedged = blocked(feet);

    /// Whether a box at `at` has something under it to stand on.
    ///
    /// The test is the box dropped by a step height, and it is vanilla's. It reads
    /// oddly until you notice that `PlayerBox::intersects` treats touching as not
    /// overlapping: a box resting exactly on a surface overlaps nothing, so "is there
    /// floor here" cannot be asked at the feet themselves. Dropping by `kStepHeight`
    /// also gets the answer right at the top of a step, where there is ground but it
    /// is not directly underfoot.
    const auto supported = [&](vec3 at) {
        at.y -= WalkMove::kStepHeight;
        return blocked(at);
    };

    /// Shortens one axis of a sneaking move until it lands somewhere supported.
    ///
    /// Bounded by construction: every pass either zeroes the value or shrinks its
    /// magnitude by a fixed increment.
    const auto backOffFromEdge = [&](f32 delta, bool alongX) {
        while (delta != 0.0f) {
            vec3 probe = feet;
            (alongX ? probe.x : probe.z) += delta;
            if (supported(probe)) {
                break;
            }
            if (delta > -WalkMove::kEdgeBackOff && delta < WalkMove::kEdgeBackOff) {
                delta = 0.0f;
            } else {
                delta -= delta > 0.0f ? WalkMove::kEdgeBackOff : -WalkMove::kEdgeBackOff;
            }
        }
        return delta;
    };

    const auto tryAxis = [&](f32 x, f32 z) {
        // **Only on the ground, and never while wedged.** Sneaking in mid-air is
        // falling, and there is no ledge to be held back from; and a player with
        // terrain already inside them has to be able to move at all, which is the
        // same escape the wedge check below grants.
        if (sneaking && onGround && !wedged) {
            if (x != 0.0f) {
                x = backOffFromEdge(x, true);
            }
            if (z != 0.0f) {
                z = backOffFromEdge(z, false);
            }
        }

        if (x == 0.0f && z == 0.0f) {
            return;
        }

        vec3 candidate = feet;
        candidate.x += x;
        candidate.z += z;

        if (wedged || !blocked(candidate)) {
            feet = candidate;
            return;
        }

        // Blocked. Try stepping up onto it, which is what makes a slab or a slope
        // walkable rather than a wall.
        if (!onGround) {
            return;
        }

        for (f32 lift = WalkMove::kStepProbe; lift <= WalkMove::kStepHeight + 1e-3f;
             lift += WalkMove::kStepProbe) {
            vec3 lifted = candidate;
            lifted.y += lift;
            if (!blocked(lifted)) {
                // Left slightly above the surface; the caller's ground probe snaps
                // the feet down onto it in the same frame.
                feet = lifted;
                return;
            }
        }
    };

    tryAxis(dx, 0.0f);
    tryAxis(0.0f, dz);

    return feet;
}

} // namespace mc
