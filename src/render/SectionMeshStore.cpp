#include "render/SectionMeshStore.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "core/Profile.hpp"

#include <cstddef>

namespace mc {
namespace {

/// Quads are 8 bytes and the shader indexes them as an array from the start of the
/// buffer, so every range has to begin on a whole quad.
constexpr usize kQuadAlignment = sizeof(Quad);

} // namespace

SectionMeshStore::SectionMeshStore(usize capacityBytes)
    : m_buffer(rhi::Buffer::createPersistent(capacityBytes)), m_allocator(capacityBytes) {
    logInfo("Mesh arena: {} MiB persistently mapped", capacityBytes / (1024 * 1024));
}

bool SectionMeshStore::store(SectionPos pos, const ChunkMesh& mesh, u64 frame) {
    MC_PROFILE_SCOPE_N("SectionMeshStore::store");

    if (mesh.empty()) {
        release(pos, frame);
        return true;
    }

    const usize byteSize = mesh.byteSize();

    usize offset = RangeAllocator::kInvalidOffset;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);

        offset = m_allocator.allocate(byteSize, kQuadAlignment);
        if (offset == RangeAllocator::kInvalidOffset) {
            return false;
        }

        // Retire the previous range rather than writing over it: the GPU may still
        // be reading it this very frame.
        const auto existing = m_placements.find(pos);
        if (existing != m_placements.end()) {
            m_pending.push_back(Pending{existing->second.byteOffset,
                                        static_cast<usize>(existing->second.quadCount)
                                            * sizeof(Quad),
                                        frame});
        }

        m_placements[pos] = Placement{offset, static_cast<u32>(mesh.quadCount())};
    }

    // Outside the lock. This is the expensive part and it touches only the range
    // just handed out, which by construction no other thread holds.
    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(mesh.quads.data()), byteSize};
    m_buffer.write(offset, bytes);

    return true;
}

void SectionMeshStore::release(SectionPos pos, u64 frame) {
    const std::lock_guard<std::mutex> lock(m_mutex);

    const auto found = m_placements.find(pos);
    if (found == m_placements.end()) {
        return;
    }

    m_pending.push_back(Pending{found->second.byteOffset,
                                static_cast<usize>(found->second.quadCount) * sizeof(Quad),
                                frame});
    m_placements.erase(found);
}

std::optional<SectionMeshStore::Placement> SectionMeshStore::find(SectionPos pos) const {
    const std::lock_guard<std::mutex> lock(m_mutex);

    const auto found = m_placements.find(pos);
    if (found == m_placements.end()) {
        return std::nullopt;
    }
    return found->second;
}

void SectionMeshStore::recycle(u64 frame) {
    MC_PROFILE_SCOPE_N("SectionMeshStore::recycle");

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

usize SectionMeshStore::sectionCount() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_placements.size();
}

usize SectionMeshStore::usedBytes() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator.used();
}

usize SectionMeshStore::largestFreeBlock() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_allocator.largestFreeBlock();
}

usize SectionMeshStore::pendingReuseBytes() const {
    const std::lock_guard<std::mutex> lock(m_mutex);

    usize total = 0;
    for (const Pending& entry : m_pending) {
        total += entry.byteSize;
    }
    return total;
}

} // namespace mc
