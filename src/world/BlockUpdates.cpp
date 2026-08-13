#include "world/BlockUpdates.hpp"

#include "core/Log.hpp"
#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "world/FallingBlocks.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>

namespace mc {

void BlockUpdates::schedule(BlockPos pos, u32 delayTicks) {
    if (!isValidWorldY(pos.y)) {
        return;
    }
    if (!m_queued.insert(pos).second) {
        return; // Already waiting to be examined.
    }
    if (m_queue.size() >= kMaxPending) {
        m_queued.erase(pos);
        if (!m_warnedFull) {
            logWarn("Block update queue full at {} entries; dropping updates. "
                    "This is a cascade that does not terminate, not a busy world.",
                    kMaxPending);
            m_warnedFull = true;
        }
        return;
    }
    m_queue.push_back(Entry{pos, m_tick + delayTicks});
}

void BlockUpdates::notify(BlockPos pos) {
    schedule(pos);
    schedule(BlockPos{pos.x - 1, pos.y, pos.z});
    schedule(BlockPos{pos.x + 1, pos.y, pos.z});
    schedule(BlockPos{pos.x, pos.y - 1, pos.z});
    schedule(BlockPos{pos.x, pos.y + 1, pos.z});
    schedule(BlockPos{pos.x, pos.y, pos.z - 1});
    schedule(BlockPos{pos.x, pos.y, pos.z + 1});
}

void BlockUpdates::notifyFluid(BlockPos pos) {
    schedule(pos, kFluidDelayTicks);
    schedule(BlockPos{pos.x - 1, pos.y, pos.z}, kFluidDelayTicks);
    schedule(BlockPos{pos.x + 1, pos.y, pos.z}, kFluidDelayTicks);
    schedule(BlockPos{pos.x, pos.y - 1, pos.z}, kFluidDelayTicks);
    schedule(BlockPos{pos.x, pos.y + 1, pos.z}, kFluidDelayTicks);
    schedule(BlockPos{pos.x, pos.y, pos.z - 1}, kFluidDelayTicks);
    schedule(BlockPos{pos.x, pos.y, pos.z + 1}, kFluidDelayTicks);
}

void BlockUpdates::clear() {
    m_queue.clear();
    m_queued.clear();
    m_warnedFull = false;
}

BlockUpdates::Stats BlockUpdates::tick(World& world, FallingBlocks& falling) {
    MC_PROFILE_SCOPE_N("BlockUpdates::tick");

    ++m_tick;

    Stats stats;
    if (m_queue.empty()) {
        return stats;
    }

    // Split the queue into what is due now and what is not, then examine the due
    // half from a copy. Examining in place would be a loop over a container that
    // `schedule` appends to -- and it appends constantly, because a block that falls
    // notifies the one above it.
    std::vector<Entry> due;
    std::vector<Entry> later;
    for (const Entry& entry : m_queue) {
        (entry.due <= m_tick ? due : later).push_back(entry);
    }
    m_queue = std::move(later);
    for (const Entry& entry : due) {
        m_queued.erase(entry.pos);
    }

    for (const Entry& entry : due) {
        ++stats.examined;
        switch (examine(world, falling, entry.pos)) {
            case Outcome::Done:
                break;
            case Outcome::Fell:
                ++stats.fell;
                break;
            case Outcome::Flowed:
                ++stats.flowed;
                break;
            case Outcome::Retry:
                schedule(entry.pos);
                ++stats.retried;
                break;
            case Outcome::Suspend:
                // A neighbour column has not arrived. Come back for it rather than
                // deciding without it -- deciding would mean reading air where there
                // is unloaded terrain, and pouring water off the edge of the world.
                schedule(entry.pos, kFluidDelayTicks);
                ++stats.suspended;
                break;
            case Outcome::Discard:
                ++stats.discarded;
                break;
        }
    }

    return stats;
}

BlockUpdates::Outcome BlockUpdates::examine(World& world, FallingBlocks& falling,
                                            BlockPos pos) {
    if (!isValidWorldY(pos.y)) {
        return Outcome::Discard;
    }

    const BlockId block = world.blockAt(pos);

    // **The fluid half.** Two behaviours share this function now; see the note in the
    // header about why that is still an `if` rather than a table.
    //
    // **Only actual fluid is examined, never air**, and that distinction is the whole
    // correctness of the flow. The first version also examined air next to water --
    // reasoning that a block just broken beside a lake is air, and air is where water
    // goes -- and it does not settle: an air block that decides its own level from
    // its neighbours bypasses both the down-first rule and the slope search, so water
    // filled every reachable cell in every direction and the queue grew without
    // bound. Air becomes water only by a *neighbour spreading into it*, which is the
    // step that carries those rules. A broken block still floods, because breaking it
    // notifies the six neighbours and the water among them is what answers.
    if (isFluid(block)) {
        return examineFluid(world, pos, block);
    }

    if (!isFalling(block)) {
        // Air, stone, or anything else that stays where it is put. This is also the
        // branch an unloaded or still-generating column takes, because `blockAt`
        // answers air for both -- see below for why that is safe *here* and will not
        // be for the next behaviour added to this function.
        return Outcome::Done;
    }

    // Nothing falls out of the bottom of the world. Bedrock makes this unreachable
    // in a generated world; it is here because a test world need not have a floor.
    if (pos.y <= kWorldMinY) {
        return Outcome::Done;
    }

    const BlockPos below{pos.x, pos.y - 1, pos.z};

    // **`below` is in the same column as `pos`, and that is what makes this safe.**
    // `World::blockAt` returns air for a column that is not loaded or is still
    // generating, so a horizontal neighbour read would happily conclude that sand at
    // the edge of the loaded region is unsupported and drop it into a column that
    // has simply not arrived yet. Only the Y differs here, so the column is the same
    // one -- and if it were not Ready, the `isFalling` test above already returned.
    // Flowing water spreads sideways and does need one: `World::isReadyAt`, which
    // `examineFluid` above calls before every horizontal read.
    if (isSolidBlock(world.blockAt(below))) {
        return Outcome::Done; // Supported.
    }

    switch (world.setBlock(pos, kAirBlock)) {
        case World::EditStatus::Applied:
        case World::EditStatus::Unchanged:
            falling.spawn(pos, block);
            // Whatever was resting on this block now has nothing under it. One tick
            // per block is what gives a collapsing pillar its cascade rather than
            // making it vanish all at once, which is vanilla's look and falls out of
            // the queue rather than being arranged.
            notify(pos);
            return Outcome::Fell;

        case World::EditStatus::Busy:
            // A meshing job owns the column. Every writer of setBlock retries rather
            // than giving up; giving up here would leave sand hanging in the air
            // with nothing left to ask about it again.
            return Outcome::Retry;

        case World::EditStatus::OutsideWorld:
        case World::EditStatus::NotLoaded:
            return Outcome::Discard;
    }

    return Outcome::Discard;
}

namespace {

/// The four horizontal neighbours, in a fixed order so a flow is deterministic.
constexpr std::array<BlockPos, 4> kSides{BlockPos{-1, 0, 0}, BlockPos{1, 0, 0},
                                         BlockPos{0, 0, -1}, BlockPos{0, 0, 1}};

BlockPos offsetBy(BlockPos pos, BlockPos delta) {
    return BlockPos{pos.x + delta.x, pos.y + delta.y, pos.z + delta.z};
}

/// How far a fluid looks for somewhere to fall before choosing a direction.
///
/// Vanilla's number for water, and it is the whole reason water finds the lip of a
/// cliff instead of creeping outward as a disc. The wiki is explicit that it exists
/// "for aesthetic purposes"; it is also what makes a flow look like it is obeying
/// gravity rather than diffusing.
constexpr i32 kSlopeSearch = 5;

/// Can water enter this position, and is the answer knowable yet?
struct Probe {
    bool ready = false;    ///< The column has finished generating.
    bool replaceable = false;
    BlockId block = kAirBlock;
};

Probe probe(const World& world, BlockPos pos) {
    Probe result;
    if (!isValidWorldY(pos.y)) {
        // Outside the world vertically is a real answer, not a missing one: nothing
        // is there and nothing ever will be.
        result.ready = true;
        return result;
    }
    result.ready = world.isReadyAt(pos);
    if (!result.ready) {
        return result;
    }
    result.block = world.blockAt(pos);
    result.replaceable = isFluidReplaceable(result.block);
    return result;
}

} // namespace

BlockUpdates::Outcome BlockUpdates::examineFluid(World& world, BlockPos pos,
                                                 BlockId block) {
    // Every decision below reads horizontal neighbours, so the position's own column
    // has to be Ready before any of it means anything. `blockAt` already answered
    // with real voxels to get here -- it returns air for a column that is not -- so
    // this is belt and braces rather than the load-bearing check.
    if (!world.isReadyAt(pos)) {
        return Outcome::Discard;
    }

    const BlockPos abovePos{pos.x, pos.y + 1, pos.z};
    const BlockPos belowPos{pos.x, pos.y - 1, pos.z};

    // -- what level should be here -------------------------------------------
    //
    // Recomputed from the neighbours every time rather than remembered. That is what
    // makes draining work without a second mechanism: remove the source and each
    // block in turn finds no supply and deletes itself, one tick per block, which is
    // the same cascade shape falling sand has.

    const bool fedFromAbove = isValidWorldY(abovePos.y) && isFluid(world.blockAt(abovePos));

    // **Water falling into this block feeds it at full strength.** Vanilla spells
    // this as levels 8-15, "falling", and the wiki puts the consequence plainly: the
    // depth resets at each new elevation, so water that has fallen down a shaft
    // spreads the full seven blocks where it lands rather than six. Level 0 here, and
    // `waterAtLevel(0)` is the falling block rather than a source -- a flow must never
    // manufacture a source, because a source never drains.
    //
    // The read is of the block directly above, so it is in this column and safe
    // without an `isReadyAt` check.
    u8 supplied = kMaxFluidLevel + 1; // "no supply", one past the last real level.
    if (fedFromAbove) {
        supplied = 0;
    } else {
        for (const BlockPos& side : kSides) {
            const BlockPos neighbour = offsetBy(pos, side);
            const Probe p = probe(world, neighbour);
            if (!p.ready) {
                // Deciding this block's level without one of its neighbours could
                // delete water that a column-that-has-not-arrived is feeding.
                return Outcome::Suspend;
            }
            if (!isFluid(p.block)) {
                continue;
            }
            const u8 candidate = static_cast<u8>(fluidLevelOf(p.block) + 1);
            supplied = std::min(supplied, candidate);
        }
    }

    const BlockId wanted = isFluidSource(block) ? block
                         : supplied > kMaxFluidLevel ? kAirBlock
                                                     : waterAtLevel(supplied);

    // **A source block is never consumed, and that is vanilla's design rather than an
    // approximation of it.** Water is not mass-conserving: flowing out of a source
    // does not deplete it, so a hole dug in the sea bed floods forever and the sea
    // does not drop. A conservative fluid would need per-body global state -- how
    // much water, where its surface is now -- which is exactly what a chunk-streaming
    // world cannot cheaply keep. See RESEARCH.md 7.1.

    Outcome outcome = Outcome::Done;

    if (block != wanted) {
        switch (world.setBlock(pos, wanted)) {
            case World::EditStatus::Applied:
            case World::EditStatus::Unchanged:
                notifyFluid(pos);
                outcome = Outcome::Flowed;
                break;
            case World::EditStatus::Busy:
                return Outcome::Retry;
            case World::EditStatus::OutsideWorld:
            case World::EditStatus::NotLoaded:
                return Outcome::Discard;
        }
        if (wanted == kAirBlock) {
            return outcome; // Nothing here to spread from any more.
        }
    }

    if (!isFluid(wanted)) {
        return outcome; // Air that no neighbour fed. Not a flow.
    }

    // -- spreading -------------------------------------------------------------

    const u8 level = fluidLevelOf(wanted);

    // **Down first, and down wins outright.** Water spreads downward with no level
    // cost at all and does not spread sideways from a block it can fall out of. That
    // asymmetry is the whole of "water runs downhill" -- falling is free and
    // sideways is metered at seven blocks.
    const Probe below = probe(world, belowPos);
    if (!below.ready) {
        return outcome == Outcome::Done ? Outcome::Suspend : outcome;
    }
    if (below.replaceable) {
        // Falling water arrives at full strength, and the block under *it* will read
        // this one as "fed from above" and do the same again -- which is what makes a
        // waterfall of any height cost no level at all.
        if (!isFluid(below.block)) {
            switch (world.setBlock(belowPos, waterAtLevel(0))) {
                case World::EditStatus::Applied:
                case World::EditStatus::Unchanged:
                    notifyFluid(belowPos);
                    outcome = Outcome::Flowed;
                    break;
                case World::EditStatus::Busy:
                    return Outcome::Retry;
                case World::EditStatus::OutsideWorld:
                case World::EditStatus::NotLoaded:
                    break;
            }
        }
        return outcome;
    }

    // Standing on something solid. Spread sideways, one level weaker, if there is a
    // level left to give.
    const auto nextLevel = static_cast<u8>(level + 1);
    if (nextLevel > kMaxFluidLevel) {
        return outcome;
    }

    // **Which sides, and this is where the slope search earns its keep.** Vanilla
    // looks up to five blocks for somewhere the water could fall and, if it finds
    // one, flows *only* toward it. Without this a flow spreads as a square disc and
    // reaches a cliff edge by accident; with it, water visibly seeks the drop.
    std::array<bool, 4> preferred{};
    bool anyPreferred = false;
    for (usize i = 0; i < kSides.size(); ++i) {
        BlockPos walk = pos;
        for (i32 step = 0; step < kSlopeSearch; ++step) {
            walk = offsetBy(walk, kSides[i]);
            const Probe ahead = probe(world, walk);
            if (!ahead.ready) {
                return Outcome::Suspend;
            }
            if (!ahead.replaceable) {
                break; // Blocked; this direction reaches no hole within reach.
            }
            const Probe under = probe(world, BlockPos{walk.x, walk.y - 1, walk.z});
            if (!under.ready) {
                return Outcome::Suspend;
            }
            if (under.replaceable) {
                preferred[i] = true;
                anyPreferred = true;
                break;
            }
        }
    }

    for (usize i = 0; i < kSides.size(); ++i) {
        if (anyPreferred && !preferred[i]) {
            continue;
        }
        const BlockPos neighbour = offsetBy(pos, kSides[i]);
        const Probe side = probe(world, neighbour);
        if (!side.ready) {
            return Outcome::Suspend;
        }
        if (!side.replaceable) {
            continue;
        }
        // Only ever make a neighbour wetter, never weaker: a level-3 block must not
        // overwrite the level-1 arriving from the other direction.
        if (isFluid(side.block) && fluidLevelOf(side.block) <= nextLevel) {
            continue;
        }
        switch (world.setBlock(neighbour, waterAtLevel(nextLevel))) {
            case World::EditStatus::Applied:
            case World::EditStatus::Unchanged:
                notifyFluid(neighbour);
                outcome = Outcome::Flowed;
                break;
            case World::EditStatus::Busy:
                return Outcome::Retry;
            case World::EditStatus::OutsideWorld:
            case World::EditStatus::NotLoaded:
                break;
        }
    }

    return outcome;
}

} // namespace mc
