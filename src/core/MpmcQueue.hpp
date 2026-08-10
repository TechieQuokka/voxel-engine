#pragma once

#include "core/Assert.hpp"
#include "core/Types.hpp"

#include <atomic>
#include <bit>
#include <utility>
#include <vector>

namespace mc {

/// Bounded lock-free multi-producer multi-consumer queue (Vyukov's algorithm).
///
/// Every slot carries a sequence number, which is what removes the need for a
/// lock: a producer claims a slot only when that slot's sequence equals the
/// position it read, and publishes by storing the next sequence. A consumer does
/// the mirror image. No thread ever waits on another, and neither push nor pop
/// allocates.
///
/// **Bounded is a feature here, not a limitation.** An unbounded queue would let
/// chunk streaming enqueue far more work than the pool can retire, so the memory
/// for pending jobs would grow with how fast the camera moves. A full queue
/// instead makes `tryPush` fail, which the caller turns into backpressure by
/// retrying next frame.
///
/// Capacity is rounded up to a power of two so that mapping a position to a slot
/// is a mask rather than a division -- the same reasoning as the 1/2/4/8-bit
/// restriction in core/BitPack.hpp.
///
/// T must be default-constructible. Slots are constructed up front and reused,
/// so a `T` is assigned into an existing slot rather than constructed in place.
template <typename T>
class MpmcQueue {
public:
    /// `minimumCapacity` is rounded up to the next power of two, minimum 2.
    explicit MpmcQueue(usize minimumCapacity)
        : m_cells(roundedCapacity(minimumCapacity)), m_mask(m_cells.size() - 1) {
        for (usize i = 0; i < m_cells.size(); ++i) {
            m_cells[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    /// Returns false when the queue is full.
    bool tryPush(T value) {
        Cell* cell = nullptr;
        usize pos = m_tail.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_cells[pos & m_mask];
            const usize sequence = cell->sequence.load(std::memory_order_acquire);
            const i64 diff = static_cast<i64>(sequence) - static_cast<i64>(pos);

            if (diff == 0) {
                // The slot is free and still at the sequence we expect. Claim it.
                if (m_tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
                // Another producer took it; `pos` now holds the updated tail.
            } else if (diff < 0) {
                return false; // The slot still holds an unconsumed value: full.
            } else {
                // A producer advanced the tail past our read. Re-read and retry.
                pos = m_tail.load(std::memory_order_relaxed);
            }
        }

        cell->value = std::move(value);
        // Release: a consumer that sees this sequence must also see the value.
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /// Returns false when the queue is empty. `out` is untouched in that case.
    bool tryPop(T& out) {
        Cell* cell = nullptr;
        usize pos = m_head.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_cells[pos & m_mask];
            const usize sequence = cell->sequence.load(std::memory_order_acquire);
            const i64 diff = static_cast<i64>(sequence) - static_cast<i64>(pos + 1);

            if (diff == 0) {
                if (m_head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // Nothing published at this position yet: empty.
            } else {
                pos = m_head.load(std::memory_order_relaxed);
            }
        }

        out = std::move(cell->value);
        // Hand the slot to the producer one lap ahead.
        cell->sequence.store(pos + m_mask + 1, std::memory_order_release);
        return true;
    }

    usize capacity() const noexcept { return m_cells.size(); }

    /// Approximate, and only meaningful when no other thread is touching the
    /// queue. Provided for statistics and tests, never for control flow.
    usize sizeApprox() const noexcept {
        const usize tail = m_tail.load(std::memory_order_relaxed);
        const usize head = m_head.load(std::memory_order_relaxed);
        return tail - head;
    }

private:
    /// Padded to a cache line: without this, two producers claiming adjacent
    /// slots would fight over the same line and the queue would be slower than a
    /// mutex.
    struct alignas(kCacheLineSize) Cell {
        std::atomic<usize> sequence{0};
        T value{};
    };

    static usize roundedCapacity(usize requested) {
        MC_VERIFY_MSG(requested > 0, "MpmcQueue capacity must be positive");
        return std::bit_ceil(requested < 2 ? usize{2} : requested);
    }

    std::vector<Cell> m_cells;
    usize m_mask;

    // Separate lines as well: the producer end and the consumer end are written
    // by different threads and must not share one.
    alignas(kCacheLineSize) std::atomic<usize> m_tail{0};
    alignas(kCacheLineSize) std::atomic<usize> m_head{0};
};

} // namespace mc
