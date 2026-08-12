#include "world/FallingBlocks.hpp"

#include "core/Profile.hpp"
#include "world/BlockTable.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>

namespace mc {

void FallingBlocks::spawn(BlockPos from, BlockId block) {
    if (block == kAirBlock || !isValidWorldY(from.y)) {
        return;
    }

    m_blocks.push_back(FallingBlock{from.x, from.z, static_cast<f32>(from.y), 0.0f, block});
}

FallingBlocks::Result FallingBlocks::tick(World& world, f32 dt) {
    MC_PROFILE_SCOPE_N("FallingBlocks::tick");

    Result result;
    if (m_blocks.empty()) {
        return result;
    }

    // Substepped and clamped, exactly as ItemEntities is and for the same reason:
    // a frame after a stall delivers a delta long enough to move a block past the
    // floor it should have landed on. The substep cap stops a long stall becoming a
    // long catch-up, at the honest cost of falling blocks lagging real time after
    // one.
    f32 remaining = std::min(dt, kMaxStep * static_cast<f32>(kMaxSubsteps));
    while (remaining > 0.0f) {
        const f32 step = std::min(remaining, kMaxStep);
        remaining -= step;
        integrate(world, step, result);
    }

    cull(world);
    return result;
}

void FallingBlocks::integrate(World& world, f32 dt, Result& result) {
    for (FallingBlock& falling : m_blocks) {
        if (falling.block == kAirBlock) {
            continue; // Already landed this tick; waiting to be swept.
        }

        falling.velocity = std::max(falling.velocity - kGravity * dt, -kTerminalVelocity);
        falling.y += falling.velocity * dt;

        // Which cell the bottom face is in now. The static_assert in the header is
        // what guarantees this never skips one: a substep at terminal velocity moves
        // two thirds of a block.
        const auto cell = static_cast<i32>(std::floor(falling.y));

        if (!isValidWorldY(cell)) {
            falling.block = kAirBlock; // Out of the world. cull() sweeps it.
            continue;
        }

        if (world.blockAt(BlockPos{falling.x, cell, falling.z}) == kAirBlock) {
            continue; // Still falling.
        }

        // The bottom has entered a solid cell, so the block comes to rest on top of
        // it. Snap rather than interpolate: the resting position is a whole cell by
        // definition, and a fractional one would put the entity a hair inside the
        // floor for the frame before it becomes a block again.
        const BlockPos destination{falling.x, cell + 1, falling.z};
        falling.y = static_cast<f32>(destination.y);
        falling.velocity = 0.0f;

        if (!isValidWorldY(destination.y)) {
            falling.block = kAirBlock;
            continue;
        }

        // Something took the cell while this was in the air. Rare -- it needs a
        // placement into a cell with an entity falling through it -- but the
        // alternative to handling it is deleting a block, which is the one outcome
        // worth ruling out even at a low probability.
        if (world.blockAt(destination) != kAirBlock) {
            result.displaced.push_back(
                Displaced{vec3{static_cast<f32>(destination.x) + 0.5f,
                               static_cast<f32>(destination.y) + 0.5f,
                               static_cast<f32>(destination.z) + 0.5f},
                          falling.block});
            falling.block = kAirBlock;
            continue;
        }

        switch (world.setBlock(destination, falling.block)) {
            case World::EditStatus::Applied:
            case World::EditStatus::Unchanged:
                result.landed.push_back(destination);
                falling.block = kAirBlock;
                break;

            case World::EditStatus::Busy:
                // A meshing job owns the column. Hold position and try again next
                // frame; the entity is the only copy of this block that exists.
                break;

            case World::EditStatus::OutsideWorld:
            case World::EditStatus::NotLoaded:
                // The column went away underneath it. cull() would have caught this
                // anyway; saying so here keeps the entity from hovering forever.
                falling.block = kAirBlock;
                break;
        }
    }
}

void FallingBlocks::cull(const World& world) {
    std::erase_if(m_blocks, [&](const FallingBlock& falling) {
        if (falling.block == kAirBlock) {
            return true; // Landed, displaced, or fell out of the world.
        }
        if (!isValidWorldY(static_cast<i32>(std::floor(falling.y)))) {
            return true;
        }
        // Its column unloaded while it was in the air. The same sweep ItemEntities
        // does, and the same reason a flat list is affordable at all.
        return world.find(ChunkPos{blockToSectionCoord(falling.x),
                                   blockToSectionCoord(falling.z)}) == nullptr;
    });
}

} // namespace mc
