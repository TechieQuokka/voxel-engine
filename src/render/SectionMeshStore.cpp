#include "render/SectionMeshStore.hpp"

#include "core/Log.hpp"
#include "core/Profile.hpp"

#include <cstddef>
#include <span>

namespace mc {

SectionMeshStore::SectionMeshStore(usize capacityBytes)
    : m_buffer(rhi::Buffer::createPersistent(capacityBytes)), m_arena(capacityBytes) {
    logInfo("Mesh arena: {} MiB persistently mapped", capacityBytes / (1024 * 1024));
}

bool SectionMeshStore::store(SectionPos pos, const ChunkMesh& mesh, u64 frame) {
    MC_PROFILE_SCOPE_N("SectionMeshStore::store");

    if (mesh.empty()) {
        release(pos, frame);
        return true;
    }

    const std::optional<usize> offset =
        m_arena.reserve(pos, static_cast<u32>(mesh.quadCount()),
                        static_cast<u32>(mesh.opaqueQuads),
                        static_cast<u32>(mesh.cutoutQuads), frame);
    if (!offset.has_value()) {
        return false;
    }

    // Outside the arena's lock. This is the expensive part and it touches only the
    // range just handed out, which by construction no other thread holds.
    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(mesh.quads.data()), mesh.byteSize()};
    m_buffer.write(*offset, bytes);

    return true;
}

} // namespace mc
