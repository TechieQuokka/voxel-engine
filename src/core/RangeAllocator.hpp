#pragma once

#include "core/Types.hpp"

#include <vector>

namespace mc {

/// Suballocates byte ranges out of one fixed-size region.
///
/// Section meshes live in a single large GPU buffer rather than one buffer each:
/// thousands of GL buffer objects cost thousands of bind calls and deny the driver
/// any chance of keeping the data contiguous. This is the bookkeeping half of that
/// arrangement, and it deliberately contains no GL at all -- the allocation policy
/// is pure integer arithmetic with sharp edge cases, which is exactly the kind of
/// thing that should be unit-testable on its own.
///
/// First-fit over an offset-sorted free list, coalescing on release. First-fit
/// rather than best-fit because section meshes are re-meshed at the same size far
/// more often than they change size, so the block just freed is usually the right
/// one to hand back, and scanning for a tighter fit only costs time.
class RangeAllocator {
public:
    static constexpr usize kInvalidOffset = ~usize{0};

    explicit RangeAllocator(usize capacity);

    /// Returns a byte offset, or kInvalidOffset when no free block fits.
    ///
    /// Failure is a normal outcome, not an error: the caller answers it by
    /// releasing a distant section's mesh, or by leaving that section unmeshed for
    /// a frame. `alignment` must be a power of two.
    usize allocate(usize size, usize alignment = 1);

    /// `offset` and `size` must be exactly what allocate() returned and was asked
    /// for -- this is not a heap and does not record block sizes for you.
    void release(usize offset, usize size);

    void reset();

    usize capacity() const noexcept { return m_capacity; }
    usize used() const noexcept { return m_used; }
    usize available() const noexcept { return m_capacity - m_used; }

    /// The largest single allocation that could currently succeed at alignment 1.
    /// The gap between this and available() is the fragmentation.
    usize largestFreeBlock() const noexcept;
    usize freeBlockCount() const noexcept { return m_free.size(); }

private:
    struct Block {
        usize offset;
        usize size;

        usize end() const noexcept { return offset + size; }
    };

    /// Sorted by offset, and never adjacent -- release() coalesces, so two blocks
    /// that touch are always merged into one. Both properties are what make the
    /// scans in this class linear in the number of holes rather than of blocks.
    std::vector<Block> m_free;

    usize m_capacity;
    usize m_used = 0;
};

} // namespace mc
