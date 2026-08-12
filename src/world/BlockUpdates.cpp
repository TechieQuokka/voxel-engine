#include "world/BlockUpdates.hpp"

#include "core/Log.hpp"
#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "world/FallingBlocks.hpp"
#include "world/World.hpp"

#include <algorithm>

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
            case Outcome::Retry:
                schedule(entry.pos);
                ++stats.retried;
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
    // Flowing water spreads sideways and will need a real "is this loaded" test.
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

} // namespace mc
