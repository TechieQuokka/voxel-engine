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

} // namespace mc
