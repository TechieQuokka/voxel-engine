#pragma once

#include "core/RangeAllocator.hpp"
#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "rhi/Buffer.hpp"
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

/// Every section mesh currently on the GPU, in one persistently mapped buffer.
///
/// One buffer rather than one per section. Thousands of buffer objects would mean
/// thousands of binds per frame and would leave the driver no way to keep the data
/// together; a single arena lets the whole visible set be drawn with one
/// multi-draw, which is what makes render distance 16 affordable at all.
///
/// **Reuse is deferred by kReuseDelayFrames.** A freed range may still be read by
/// a frame the GPU has not finished, and a coherent mapping gives no protection
/// against that -- coherence orders writes, it does not know what the GPU is
/// reading. Holding released ranges for a few frames is the cheap form of the
/// triple buffering DESIGN.md 3.8 calls for, and it is sufficient because nothing
/// here overwrites a range in place: an updated mesh always takes a fresh range.
class SectionMeshStore {
public:
    /// Three frames: the GPU is at most two behind with a triple-buffered
    /// swapchain, and the third is slack for a driver that queues one more.
    static constexpr u64 kReuseDelayFrames = 3;

    /// Where one section's quads live in the arena.
    struct Placement {
        usize byteOffset = 0;
        u32 quadCount = 0;
    };

    /// `capacityBytes` is fixed for the store's lifetime, because the arena is
    /// immutable GL storage. Sizing it is a budget decision, not a guess: see
    /// kDefaultCapacityBytes.
    explicit SectionMeshStore(usize capacityBytes);

    /// Uploads (or replaces) one section's mesh.
    ///
    /// Returns false when the arena is full, which is a normal outcome the caller
    /// answers by trying again next frame after distant sections are released.
    /// An empty mesh releases the section's storage and returns true.
    bool store(SectionPos pos, const ChunkMesh& mesh, u64 frame);

    /// Drops a section's storage. Silently ignores a section that has none.
    void release(SectionPos pos, u64 frame);

    std::optional<Placement> find(SectionPos pos) const;

    /// Returns ranges released at least kReuseDelayFrames ago to the allocator.
    /// Call once per frame, before the frame's store() calls.
    void recycle(u64 frame);

    const rhi::Buffer& buffer() const noexcept { return m_buffer; }

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
    /// data path -- the memcpy into the mapped buffer -- is outside the lock, which
    /// is the part that actually needs to be concurrent.
    mutable std::mutex m_mutex;

    rhi::Buffer m_buffer;
    RangeAllocator m_allocator;
    std::unordered_map<SectionPos, Placement, SectionPosHash> m_placements;
    std::vector<Pending> m_pending;
};

/// Arena size for a given render distance.
///
/// Derived from measurement, and re-derived once caves existed — which is the whole
/// reason this is a function and not a constant.
///
/// Before carvers a fully meshed distance-16 region was ~12 MiB of quads, about 11 KiB
/// per column, and 48 KiB was a comfortable 4x margin. Caves changed that by a lot:
/// thin tunnels have an enormous surface-to-volume ratio, so a 1.7% air fraction
/// underground produced 50 KiB per column — right at the old budget. Distance 16 then
/// wedged with a permanently full arena.
///
/// 176 KiB per column is the cave-era measurement with headroom: distance 16 settles at
/// 112 MiB of the 136 that 128 KiB/column bought, which is closer to full than a fixed
/// allocation should ever run. LOD in Phase 6 pulls it back down for distant chunks;
/// occlusion culling in Phase 8 stops those cave surfaces being *drawn*, but they still
/// have to be stored.
///
/// The arena is immutable GL storage and persistently mapped, so this is pinned
/// memory that exists whether or not it is used — which is exactly why it is scaled
/// to the render distance instead of being one fixed number large enough for the
/// eventual distance-64 target.
inline usize meshArenaBytesFor(i32 renderDistance) {
    constexpr usize kBytesPerColumn = 176u * 1024u;
    constexpr usize kMinimum = 32u * 1024u * 1024u;
    constexpr usize kMaximum = 768u * 1024u * 1024u;

    const auto side = static_cast<usize>(2 * (renderDistance > 0 ? renderDistance : 1) + 1);
    const usize estimate = side * side * kBytesPerColumn;

    return estimate < kMinimum ? kMinimum : (estimate > kMaximum ? kMaximum : estimate);
}

} // namespace mc
