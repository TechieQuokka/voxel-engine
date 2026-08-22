#pragma once

#include "core/Types.hpp"

#include <cmath>

namespace mc {

/// How much a fall hurts.
///
/// **One function, in `world`, because the rule is a fact about the game and not
/// about the engine that runs it.** It lived as a private constant and a private
/// method on `Engine`, where nothing could reach it -- the same shape as the pickup
/// radius in `PlayerBox`'s header, which this project has already paid for once. The
/// height a player learns to fear is a parity decision (RESEARCH-grade, comparable
/// against vanilla), and a parity decision that cannot be asserted is a parity
/// decision nobody will notice breaking.
///
/// The caller still owns everything around it: tracking the fall, subtracting the
/// health, and deciding what death means. This answers only the arithmetic.
struct FallDamage {
    /// Vanilla's rule: no damage for the first three blocks, then one half-heart per
    /// block after that. Three is what makes a jump free and a two-storey drop hurt.
    static constexpr f32 kSafeBlocks = 3.0f;

    /// Half-hearts of damage for a fall of `blocks`, floored, never negative.
    ///
    /// At 20 health this makes a 23-block drop fatal, which is close enough to the
    /// real thing that the height a player learns to fear transfers.
    static f32 forDistance(f32 blocks) noexcept {
        if (!(blocks > kSafeBlocks)) {
            // Written as a negated `>` so a NaN distance answers zero rather than
            // propagating into the player's health, where it would make every later
            // comparison false and leave them permanently neither alive nor dead.
            return 0.0f;
        }
        const f32 damage = std::floor(blocks - kSafeBlocks);
        return damage > 0.0f ? damage : 0.0f;
    }
};

/// Remembers where a fall started, so landing can price it.
///
/// **This was three fields and four scattered assignments inside `Engine::updateWalk`,
/// and one of the four was missing.** The substep loop began a fall when it saw the
/// player had been on the ground the step before -- but jumping cleared `onGround`
/// *before* the loop ever read it, so a fall that started with a jump was never
/// tracked and landed for free at any height. Walking off a ledge tracked correctly,
/// which is exactly what made it look like damage was capped rather than absent.
///
/// Here it is a type with four named transitions, in `world`, where a test can reach
/// it. `FallDamage` above is the same argument one step earlier: a rule nobody can
/// assert is a rule nobody will notice breaking.
class FallTracker {
public:
    /// The player left the ground, by jumping or by walking off something.
    ///
    /// **Measured from where the ground was left rather than from the peak**, so a
    /// jump costs its own arc -- which is why the three-block grace exists at all.
    /// Calling this again while already tracking is ignored: a fall has one origin.
    void leftGround(f32 y) noexcept {
        if (!m_tracking) {
            m_from = y;
            m_tracking = true;
        }
    }

    /// Still going up. The fall has not begun, so the origin follows the player.
    void rose(f32 y) noexcept {
        if (m_tracking && y > m_from) {
            m_from = y;
        }
    }

    /// Something cancelled the fall outright: entering water, or ground that has not
    /// streamed in yet. Neither should cost the player health.
    void cancel() noexcept { m_tracking = false; }

    /// Landed at `y`. Returns how far the fall was, or 0 when nothing was being
    /// tracked. Stops tracking either way.
    f32 landed(f32 y) noexcept {
        if (!m_tracking) {
            return 0.0f;
        }
        m_tracking = false;
        const f32 distance = m_from - y;
        return distance > 0.0f ? distance : 0.0f;
    }

    bool tracking() const noexcept { return m_tracking; }
    f32 from() const noexcept { return m_from; }

private:
    f32 m_from = 0.0f;
    bool m_tracking = false;
};

} // namespace mc
