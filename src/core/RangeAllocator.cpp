#include "core/RangeAllocator.hpp"

#include "core/Assert.hpp"

#include <algorithm>
#include <bit>

namespace mc {
namespace {

usize alignUp(usize value, usize alignment) {
    MC_ASSERT(std::has_single_bit(alignment));
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

RangeAllocator::RangeAllocator(usize capacity) : m_capacity(capacity) {
    MC_VERIFY(capacity > 0);
    m_free.push_back(Block{0, capacity});
}

usize RangeAllocator::allocate(usize size, usize alignment) {
    MC_ASSERT(size > 0);
    MC_ASSERT(std::has_single_bit(alignment));

    for (usize i = 0; i < m_free.size(); ++i) {
        const Block block = m_free[i];
        const usize aligned = alignUp(block.offset, alignment);

        // The alignment padding comes out of this block, so a block can be large
        // enough by size and still not fit.
        if (aligned + size > block.end()) {
            continue;
        }

        const usize headSize = aligned - block.offset;
        const usize tailOffset = aligned + size;
        const usize tailSize = block.end() - tailOffset;

        // Rewrite this slot as the head remainder, or drop it, then insert the tail
        // after it. Order is preserved either way, so the list stays sorted.
        if (headSize > 0) {
            m_free[i] = Block{block.offset, headSize};
            if (tailSize > 0) {
                m_free.insert(m_free.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                              Block{tailOffset, tailSize});
            }
        } else if (tailSize > 0) {
            m_free[i] = Block{tailOffset, tailSize};
        } else {
            m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i));
        }

        // Alignment padding is *not* consumed: the head remainder went back on the
        // free list above, so it stays allocatable. Counting only `size` is what
        // keeps this symmetric with release(), which is given only a size.
        m_used += size;
        return aligned;
    }

    return kInvalidOffset;
}

void RangeAllocator::release(usize offset, usize size) {
    MC_ASSERT(size > 0);
    MC_VERIFY_MSG(offset + size <= m_capacity, "releasing a range outside the arena");

    // Insertion point, keeping the list sorted by offset.
    const auto at = std::lower_bound(
        m_free.begin(), m_free.end(), offset,
        [](const Block& block, usize value) { return block.offset < value; });

    MC_ASSERT_MSG(at == m_free.end() || at->offset >= offset + size,
                  "released range overlaps a free block -- double release?");
    MC_ASSERT_MSG(at == m_free.begin() || std::prev(at)->end() <= offset,
                  "released range overlaps a free block -- double release?");

    const auto inserted = m_free.insert(at, Block{offset, size});

    // Coalesce forward first, then backward. Doing it in this order means the
    // backward merge sees the already-merged size and one pass is enough.
    const auto next = std::next(inserted);
    if (next != m_free.end() && inserted->end() == next->offset) {
        inserted->size += next->size;
        m_free.erase(next);
    }
    if (inserted != m_free.begin()) {
        const auto previous = std::prev(inserted);
        if (previous->end() == inserted->offset) {
            previous->size += inserted->size;
            m_free.erase(inserted);
        }
    }

    MC_ASSERT(m_used >= size);
    m_used -= size;
}

void RangeAllocator::reset() {
    m_free.clear();
    m_free.push_back(Block{0, m_capacity});
    m_used = 0;
}

usize RangeAllocator::largestFreeBlock() const noexcept {
    usize largest = 0;
    for (const Block& block : m_free) {
        largest = std::max(largest, block.size);
    }
    return largest;
}

} // namespace mc
