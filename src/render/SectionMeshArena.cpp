#include "render/SectionMeshArena.hpp"

#include "core/Assert.hpp"
#include "core/Profile.hpp"

namespace mc {
namespace {

/// Quads are 8 bytes and the shader indexes them as an array from the start of the
/// buffer, so every range has to begin on a whole quad.
constexpr usize kQuadAlignment = sizeof(Quad);

usize bytesFor(u32 quadCount) {
    return static_cast<usize>(quadCount) * sizeof(Quad);
}

} // namespace

std::optional<usize> SectionMeshArena::reserve(SectionPos pos, u32 quadCount,
                                               u32 opaqueCount, u32 cutoutCount,
                                               u64 frame) {
    MC_PROFILE_SCOPE_N("SectionMeshArena::reserve");
    MC_ASSERT(quadCount > 0); // An empty mesh is a release, not a reservation.
    MC_ASSERT(static_cast<u64>(opaqueCount) + cutoutCount <= quadCount);

    const std::lock_guard<std::mutex> lock(m_mutex);

    const usize offset = m_allocator.allocate(bytesFor(quadCount), kQuadAlignment);
    if (offset == RangeAllocator::kInvalidOffset) {
        return std::nullopt;
    }

    // Retire the previous range rather than writing over it: the GPU may still
    // be reading it this very frame.
    const auto existing = m_placements.find(pos);
    if (existing != m_placements.end()) {
        m_pending.push_back(
            Pending{existing->second.byteOffset, bytesFor(existing->second.quadCount), frame});
    }

    m_placements[pos] = Placement{offset, quadCount, opaqueCount, cutoutCount};
    return offset;
}

void SectionMeshArena::release(SectionPos pos, u64 frame) {
    const std::lock_guard<std::mutex> lock(m_mutex);

    const auto found = m_placements.find(pos);
    if (found == m_placements.end()) {
        return;
    }

    m_pending.push_back(
        Pending{found->second.byteOffset, bytesFor(found->second.quadCount), frame});
    m_placements.erase(found);
}

std::optional<SectionMeshArena::Placement> SectionMeshArena::find(SectionPos pos) const {
    const std::lock_guard<std::mutex> lock(m_mutex);

    const auto found = m_placements.find(pos);
    if (found == m_placements.end()) {
        return std::nullopt;
    }
    return found->second;
}

void SectionMeshArena::recycle(u64 frame) {
    MC_PROFILE_SCOPE_N("SectionMeshArena::recycle");

    const std::lock_guard<std::mutex> lock(m_mutex);

    usize keep = 0;
    for (usize i = 0; i < m_pending.size(); ++i) {
        const Pending& entry = m_pending[i];
        if (frame < entry.releasedOnFrame + kReuseDelayFrames) {
            // Still too young. Compact the survivors to the front rather than
            // erasing from the middle.
            m_pending[keep] = entry;
            ++keep;
            continue;
        }
        m_allocator.release(entry.byteOffset, entry.byteSize);
    }
    m_pending.resize(keep);
}

usize SectionMeshArena::sectionCount() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_placements.size();
}

usize SectionMeshArena::usedBytes() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator.used();
}

usize SectionMeshArena::largestFreeBlock() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator.largestFreeBlock();
}

usize SectionMeshArena::pendingReuseBytes() const {
    const std::lock_guard<std::mutex> lock(m_mutex);

    usize total = 0;
    for (const Pending& entry : m_pending) {
        total += entry.byteSize;
    }
    return total;
}

} // namespace mc
