#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"

#include <array>

namespace mc {

/// How many of each block type the player is carrying.
///
/// **Counts, not slots.** There is no ordering, no stack limit, no dragging and no
/// inventory screen -- one number per block type, indexed by `BlockId`. That is
/// enough for the whole loop that matters: break a block, watch it drop, pick it up,
/// place it and see the number go down.
///
/// The slot model is what crafting will need, because a recipe is a shape over slots
/// and a stack limit is what makes an inventory fill up. Building it now would mean
/// a UI layer -- cursor mode, hit testing, a window -- for a feature nothing yet
/// asks for. This is the smaller thing that proves the loop first.
class Inventory {
public:
    void add(BlockId block, u32 count) {
        if (block == kAirBlock || block >= kBlocks.size()) {
            return;
        }
        m_counts[block] += count;
    }

    u32 count(BlockId block) const {
        return block < kBlocks.size() ? m_counts[block] : 0u;
    }

    /// Removes one if there is one. False means the player has none, and the caller
    /// must not place anything.
    bool take(BlockId block) {
        if (block >= kBlocks.size() || m_counts[block] == 0) {
            return false;
        }
        --m_counts[block];
        return true;
    }

    usize distinctBlocks() const {
        usize kinds = 0;
        for (const u32 count : m_counts) {
            kinds += count > 0 ? 1 : 0;
        }
        return kinds;
    }

private:
    std::array<u32, kBlocks.size()> m_counts{};
};

} // namespace mc
