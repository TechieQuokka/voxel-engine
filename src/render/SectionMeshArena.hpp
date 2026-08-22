#pragma once

#include "core/RangeAllocator.hpp"
#include "core/Types.hpp"
#include "mesh/Quad.hpp"
#include "world/Coords.hpp"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mc {

/// Hash for SectionPos, built the same way as ChunkPosHash.
struct SectionPosHash {
    usize operator()(SectionPos pos) const noexcept {
        // 21 bits per axis is ±1,048,576 sections, far beyond any reachable
        // coordinate, so this packing loses nothing in practice.
        u64 h = (static_cast<u64>(static_cast<u32>(pos.x) & 0x1FFFFFu))
              | (static_cast<u64>(static_cast<u32>(pos.y) & 0x1FFFFFu) << 21)
              | (static_cast<u64>(static_cast<u32>(pos.z) & 0x1FFFFFu) << 42);
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBull;
        h ^= h >> 31;
        return static_cast<usize>(h);
    }
};

/// Who owns which range of the mesh arena, and when a freed range becomes safe to
/// hand out again.
///
/// **This is `SectionMeshStore` with the GL buffer taken out of it**, and it is a
/// separate class for one reason: no test may create a GL context, so as long as
/// this logic sat next to an `rhi::Buffer` it could not be tested at all -- while
/// being, by DESIGN.md's own account, the trickiest lifetime logic in the engine.
/// `RangeAllocator` underneath was unit-tested; the deferred reuse, the pending
/// list and the behaviour at arena exhaustion were not. The seam is the same one
/// `WalkMove` and `PlayerBox` were pulled through.
///
/// **Reuse is deferred by kReuseDelayFrames.** A freed range may still be read by a
/// frame the GPU has not finished, and a coherent mapping gives no protection
/// against that -- coherence orders writes, it does not know what the GPU is
/// reading. Holding released ranges for a few frames is the cheap form of the
/// triple buffering DESIGN.md 3.8 calls for, and it is sufficient because nothing
/// here overwrites a range in place: an updated mesh always takes a fresh range.
class SectionMeshArena {
public:
    /// Three frames: the GPU is at most two behind with a triple-buffered
    /// swapchain, and the third is slack for a driver that queues one more.
    static constexpr u64 kReuseDelayFrames = 3;

    /// Where one section's quads live in the arena.
    ///
    /// One contiguous range holding the opaque quads, then the cutout ones, then the
    /// translucent ones, with the two counts marking the joins. Three ranges would
    /// mean three allocations, three retirements and three lifetimes to keep in step
    /// for what is arithmetic on a single offset.
    ///
    /// The order matches `ChunkMesh`, and it matches the order the three passes have
    /// to be drawn in: opaque fills depth, cutout tests against it and may discard,
    /// translucent blends over both and writes no depth.
    struct Placement {
        usize byteOffset = 0;
        u32 quadCount = 0;
        u32 opaqueCount = 0;
        /// Non-cube geometry, in **boxes**, not quads. Between the opaque and cutout
        /// stretches; see mesh/ChunkMesh.hpp for the layout and mesh/ModelBox.hpp for
        /// why a box is one word of the same arena.
        u32 modelCount = 0;
        u32 cutoutCount = 0;

        u32 translucentCount() const noexcept {
            return quadCount - opaqueCount - modelCount - cutoutCount;
        }
    };

    explicit SectionMeshArena(usize capacityBytes) : m_allocator(capacityBytes) {}

    /// Reserves a range for `pos` and records the placement, returning the byte
    /// offset the caller is then to write the quads to.
    ///
    /// Returns nullopt when the arena is full, which is a normal outcome the caller
    /// answers by trying again next frame after distant sections are released.
    /// **The previous placement is left alone in that case** -- a section whose
    /// update could not be allocated keeps drawing the mesh it already had rather
    /// than vanishing until the arena drains.
    ///
    /// A zero `quadCount` is the caller's mistake; an empty mesh is a `release`.
    std::optional<usize> reserve(SectionPos pos, u32 quadCount, u32 opaqueCount,
                                 u32 modelCount, u32 cutoutCount, u64 frame);

    /// Drops a section's storage. Silently ignores a section that has none.
    void release(SectionPos pos, u64 frame);

    std::optional<Placement> find(SectionPos pos) const;

    /// Returns ranges released at least kReuseDelayFrames ago to the allocator.
    /// Call once per frame, before the frame's reserve() calls.
    void recycle(u64 frame);

    /// Fixed at construction, so this one needs no lock.
    usize capacityBytes() const noexcept { return m_allocator.capacity(); }

    // The rest read state the upload thread also writes, so they take the mutex --
    // they are called a handful of times per second for statistics, and a torn read
    // in a log line is not worth leaving as a documented race.
    usize sectionCount() const;
    usize usedBytes() const;
    usize pendingReuseBytes() const;
    usize largestFreeBlock() const;

private:
    struct Pending {
        usize byteOffset;
        usize byteSize;
        u64 releasedOnFrame;
    };

    /// Guards the allocator, the placement map and the pending list.
    ///
    /// A mutex, not a lock-free structure, and deliberately: this is touched a few
    /// hundred times a frame at most, by the upload thread and the main thread. The
    /// data path -- the memcpy into the mapped buffer -- happens outside this class
    /// entirely, which is the part that actually needs to be concurrent.
    mutable std::mutex m_mutex;

    RangeAllocator m_allocator;
    std::unordered_map<SectionPos, Placement, SectionPosHash> m_placements;
    std::vector<Pending> m_pending;
};

} // namespace mc
