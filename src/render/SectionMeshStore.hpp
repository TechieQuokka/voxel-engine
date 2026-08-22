#pragma once

#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "render/SectionMeshArena.hpp"
#include "rhi/Buffer.hpp"
#include "world/Coords.hpp"

#include <optional>

namespace mc {

/// Every section mesh currently on the GPU, in one persistently mapped buffer.
///
/// One buffer rather than one per section. Thousands of buffer objects would mean
/// thousands of binds per frame and would leave the driver no way to keep the data
/// together; a single arena lets the whole visible set be drawn with one
/// multi-draw, which is what makes render distance 16 affordable at all.
///
/// **This class is the GL half and nothing else.** Who owns which range, and when a
/// freed range is safe to reuse, is `SectionMeshArena` -- split out because a test
/// may not create a GL context, and the bookkeeping is the part with the hazards in
/// it. What is left here is a buffer, a memcpy, and the decision to do the memcpy
/// outside the lock.
class SectionMeshStore {
public:
    using Placement = SectionMeshArena::Placement;

    static constexpr u64 kReuseDelayFrames = SectionMeshArena::kReuseDelayFrames;

    /// `capacityBytes` is fixed for the store's lifetime, because the arena is
    /// immutable GL storage. Sizing it is a budget decision, not a guess: see
    /// meshArenaBytesFor.
    explicit SectionMeshStore(usize capacityBytes);

    /// Uploads (or replaces) one section's mesh.
    ///
    /// Returns false when the arena is full, which is a normal outcome the caller
    /// answers by trying again next frame after distant sections are released.
    /// An empty mesh releases the section's storage and returns true.
    bool store(SectionPos pos, const ChunkMesh& mesh, u64 frame);

    /// Drops a section's storage. Silently ignores a section that has none.
    void release(SectionPos pos, u64 frame) { m_arena.release(pos, frame); }

    std::optional<Placement> find(SectionPos pos) const { return m_arena.find(pos); }

    /// Returns ranges released at least kReuseDelayFrames ago to the allocator.
    /// Call once per frame, before the frame's store() calls.
    void recycle(u64 frame) { m_arena.recycle(frame); }

    const rhi::Buffer& buffer() const noexcept { return m_buffer; }

    usize capacityBytes() const noexcept { return m_arena.capacityBytes(); }
    usize sectionCount() const { return m_arena.sectionCount(); }
    usize usedBytes() const { return m_arena.usedBytes(); }
    usize pendingReuseBytes() const { return m_arena.pendingReuseBytes(); }
    usize largestFreeBlock() const { return m_arena.largestFreeBlock(); }

private:
    rhi::Buffer m_buffer;
    SectionMeshArena m_arena;
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
