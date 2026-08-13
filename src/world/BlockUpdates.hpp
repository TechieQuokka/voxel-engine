#pragma once

#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <unordered_set>
#include <vector>

namespace mc {

class World;
class FallingBlocks;

/// "This block changed -- tell its neighbours."
///
/// **The subsystem this engine did not have.** Everything before it reacted to an
/// edit *spatially*: `World::setBlock` works out which meshes are now wrong and
/// marks them. Nothing worked out which blocks are now wrong. Sand sat unsupported
/// in mid-air because no code anywhere asked it to look down.
///
/// Two halves, and the first is the one with reuse in it:
///
/// 1. **A queue of positions to examine, on a delay.** Notification is not
///    recursion -- a collapsing pillar must not blow the stack, and vanilla's
///    cascade one block per tick is a *look*, not an implementation detail. So an
///    edit queues its neighbours and the next tick examines them.
/// 2. **What examining a position decides**, which today is only "does this fall".
///
/// The second half is deliberately concrete rather than a table of behaviours or a
/// virtual on a block. An abstraction with one implementation is not an abstraction
/// -- the same argument DESIGN.md 7.10 makes for not pre-splitting items from
/// blocks.
///
/// **Flowing water is the second behaviour, and it arrived without changing the queue
/// at all.** The prediction above was that the shape of the dispatch would become
/// obvious once there were two; what it turned out to be is an `if` on the block type
/// at the top of `examine` and a second function under it. Two behaviours is still
/// not enough to justify a table, and the day a third arrives is the day to look
/// again -- but the *queue*, the dedupe set, the delay and the retry discipline were
/// all reused unchanged, which is what the split was for.
///
/// **Main thread only.** It calls `World::setBlock`, which is main-thread-only for
/// the pin reasons in World.hpp, and it holds no pointers into the world between
/// calls -- only positions, which stay meaningful after a column unloads because
/// looking one up is what discovers that it did.
class BlockUpdates {
public:
    /// Queues `pos` and its six face neighbours for the next tick.
    ///
    /// Six rather than the twenty-six a mesher would want: this is asking "did my
    /// support or my neighbour change", which is a face relationship. Vanilla
    /// notifies the same six.
    void notify(BlockPos pos);

    /// Queues one position, `delayTicks` ticks from now. A position already queued
    /// stays at its existing time rather than being moved -- one pending
    /// examination per position is the whole semantics, and a notification storm
    /// against one block should not be able to push its examination into the future.
    void schedule(BlockPos pos, u32 delayTicks = 1);

    struct Stats {
        usize examined = 0;
        usize fell = 0;
        /// Water blocks created, moved or removed.
        usize flowed = 0;
        /// The world was busy; re-queued rather than lost.
        usize retried = 0;
        /// Outside the world, or in a column that has gone away.
        usize discarded = 0;
        /// A neighbouring column had not finished generating, so the spread was
        /// suspended and re-queued. **Vanilla does exactly this** rather than
        /// solving it -- see `World::isReadyAt`.
        usize suspended = 0;
    };

    /// One game tick: examines everything due, and re-queues what the world refused.
    Stats tick(World& world, FallingBlocks& falling);

    usize pending() const noexcept { return m_queue.size(); }
    void clear();

    /// Queue ceiling. Past it, new notifications are dropped and complained about
    /// once.
    ///
    /// A bound rather than trust: an update that re-queues itself forever is the
    /// classic failure of this kind of system, and the symptom without a ceiling is
    /// memory growth rather than anything that points at the cause. 65,536 is far
    /// more than any legitimate cascade -- a 384-block column of sand is 384.
    static constexpr usize kMaxPending = 1u << 16;

private:
    /// What examining one position concluded.
    enum class Outcome {
        Done,     ///< Nothing to do. Air, a block that does not fall, or a supported one.
        Fell,     ///< Became a falling entity.
        Flowed,   ///< Water was created, moved or removed here.
        Retry,    ///< The column is pinned. Ask again next tick.
        Suspend,  ///< A neighbouring column is not Ready. Ask again next tick.
        Discard,  ///< Outside the world, or the column is gone.
    };

    Outcome examine(World& world, FallingBlocks& falling, BlockPos pos);

    /// The fluid half. Decides what level this position *should* hold and edits
    /// toward it, then spreads down and sideways. See the write-up in the .cpp.
    Outcome examineFluid(World& world, BlockPos pos, BlockId block);

    /// Queues `pos` and its six face neighbours `kFluidDelayTicks` out.
    ///
    /// **The delay is the flow speed.** Vanilla water moves one block per 5 ticks --
    /// 4 blocks a second at 20 Hz -- and since each block's spread queues the next
    /// one, the schedule delay *is* the rate. Using `notify`'s single tick here would
    /// make water run at 20 blocks a second, which reads as a burst rather than a
    /// flow.
    void notifyFluid(BlockPos pos);

    /// Water's speed is one block per 5 ticks, which is vanilla's and is why the
    /// fluid half schedules itself further out than the falling half does.
    static constexpr u32 kFluidDelayTicks = 5;

    struct Entry {
        BlockPos pos{};
        u64 due = 0;
    };

    std::vector<Entry> m_queue;
    /// Membership of `m_queue`, so `schedule` is not a linear scan. A cascade in
    /// loose ground queues hundreds of positions and each of them notifies seven
    /// more; the quadratic version of this is measurable and the set is four lines.
    std::unordered_set<BlockPos, BlockPosHash> m_queued;

    u64 m_tick = 0;
    bool m_warnedFull = false;
};

} // namespace mc
