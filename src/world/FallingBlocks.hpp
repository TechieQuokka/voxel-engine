#pragma once

#include "core/Math.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <vector>

namespace mc {

class World;

/// A block on its way down, between the cell it left and the cell it will land in.
///
/// **Horizontal position is integral and never changes.** A falling block in
/// Minecraft drops straight down the column it left, and storing x and z as `i32`
/// says that in the type rather than in a comment -- there is no axis for float
/// drift to accumulate on, and the landing cell is known the moment it spawns.
struct FallingBlock {
    i32 x = 0;
    i32 z = 0;
    /// The block's **bottom** face, so it occupies [y, y + 1). Landing is then
    /// "which cell is my bottom in", with no half-extent to add or subtract.
    f32 y = 0.0f;
    f32 velocity = 0.0f;

    BlockId block = kAirBlock;
};

/// Every block currently falling.
///
/// The second entity type, and it exists because sand and gravel obey gravity in
/// Minecraft and this engine had no concept of a block that is between two cells.
/// It sits beside `ItemEntities` rather than inside it: a dropped item is a quarter
/// of a block that spins, bobs, merges, despawns and can be picked up, and a falling
/// block is a full cube that does none of those and turns back into world geometry.
/// One type carrying both would be a tagged union whose two halves share the word
/// "gravity" and nothing else.
///
/// **Physics runs on frame delta time, not on the game tick.** The block-update
/// scheduler that spawns these runs at 20 Hz because that is the rate vanilla
/// notifies neighbours at, but a falling block moving in 20 Hz steps visibly
/// stutters against a 60 FPS camera. `ItemEntities` already made this choice for the
/// same reason, and the two now agree.
class FallingBlocks {
public:
    /// Turns the block at `from` into a falling entity. The caller is expected to
    /// have already set that cell to air -- this does not edit the world, so that a
    /// spawn cannot half-succeed.
    void spawn(BlockPos from, BlockId block);

    /// A block that landed somewhere it could not be put back.
    struct Displaced {
        vec3 position{};
        BlockId block = kAirBlock;
    };

    struct Result {
        /// Cells that became solid again this tick. The caller notifies these, so
        /// that whatever was resting on top of them gets to react.
        std::vector<BlockPos> landed;
        /// Landed into a cell something else had taken in the meantime. Vanilla
        /// drops these as items rather than deleting them, and so does the caller;
        /// silently destroying a block is the one outcome worth ruling out.
        std::vector<Displaced> displaced;
    };

    /// Falls, lands, and gives back what happened.
    ///
    /// A landing that the world refuses (`Busy` -- a meshing job owns the column)
    /// leaves the entity resting exactly where it stopped and retries next frame.
    /// That is the same discipline every other writer of `World::setBlock` follows,
    /// and here it is load-bearing: dropping the entity would delete the block.
    Result tick(World& world, f32 dt);

    const std::vector<FallingBlock>& blocks() const noexcept { return m_blocks; }
    usize size() const noexcept { return m_blocks.size(); }
    void clear() { m_blocks.clear(); }

    /// Matches the player's gravity and `ItemEntities`'s, so everything in this
    /// world falls at one rate rather than at three.
    static constexpr f32 kGravity = 28.0f;
    static constexpr f32 kTerminalVelocity = 40.0f;

    /// Longest physics step, and how many of them one `tick` may run.
    ///
    /// The landing test asks which cell the block's bottom is in rather than
    /// sweeping the path to it, so a substep that covers a whole block could pass
    /// straight through the floor. The static_assert below is what keeps that
    /// impossible rather than merely unlikely.
    static constexpr f32 kMaxStep = 1.0f / 60.0f;
    static constexpr u32 kMaxSubsteps = 4;

private:
    void integrate(World& world, f32 dt, Result& result);
    void cull(const World& world);

    std::vector<FallingBlock> m_blocks;
};

static_assert(FallingBlocks::kTerminalVelocity * FallingBlocks::kMaxStep < 1.0f,
              "a substep at terminal velocity must stay inside one block, or the "
              "landing test can step over the floor it was supposed to find");

} // namespace mc
