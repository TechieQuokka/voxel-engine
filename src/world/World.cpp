#include "world/World.hpp"

#include "core/Profile.hpp"

#include <algorithm>
#include <vector>

namespace mc {

World::World(i32 renderDistance) : m_renderDistance(std::max(0, renderDistance)) {}

void World::setRenderDistance(i32 chunks) {
    m_renderDistance = std::max(0, chunks);
}

bool World::isInRegion(ChunkPos pos, ChunkPos center) const noexcept {
    // Chebyshev distance: the region is the square the render distance describes.
    const i32 dx = pos.x - center.x;
    const i32 dz = pos.z - center.z;
    return std::max(std::abs(dx), std::abs(dz)) <= m_renderDistance;
}

World::LoadResult World::updateLoadedRegion(ChunkPos center) {
    MC_PROFILE_SCOPE_N("World::updateLoadedRegion");

    LoadResult result;

    // Unload first, so the peak column count is the region size rather than the
    // union of the old and new regions. At distance 16 that is the difference
    // between 1,089 columns and up to 2,178.
    std::vector<ChunkPos> dropping;
    for (const auto& [pos, chunk] : m_chunks) {
        if (isInRegion(pos, center)) {
            continue;
        }
        if (chunk->state() == ChunkState::Generating || chunk->pinned()) {
            // Either a worker is writing into it, or a meshing job is holding
            // pointers into it. Leave it and reconsider next call.
            ++result.retained;
            continue;
        }
        dropping.push_back(pos);
    }
    for (const ChunkPos pos : dropping) {
        m_chunks.erase(pos);
    }
    result.unloaded = dropping.size();
    result.unloadedPositions = std::move(dropping);

    // Create in order of increasing distance from the centre, so that when the
    // scheduler walks the map the nearest columns already exist. It does not
    // guarantee generation order -- that is what the priority bands are for -- but
    // it costs nothing here.
    for (i32 ring = 0; ring <= m_renderDistance; ++ring) {
        for (i32 dz = -ring; dz <= ring; ++dz) {
            for (i32 dx = -ring; dx <= ring; ++dx) {
                // Only the ring's boundary; the interior was done by earlier rings.
                if (std::max(std::abs(dx), std::abs(dz)) != ring) {
                    continue;
                }

                const ChunkPos pos{center.x + dx, center.z + dz};
                if (m_chunks.find(pos) != m_chunks.end()) {
                    continue;
                }

                m_chunks.emplace(pos, std::make_unique<Chunk>(pos));
                ++result.created;
            }
        }
    }

    return result;
}

void World::clear() {
    m_chunks.clear();
}

Chunk* World::find(ChunkPos pos) noexcept {
    const auto found = m_chunks.find(pos);
    return found != m_chunks.end() ? found->second.get() : nullptr;
}

const Chunk* World::find(ChunkPos pos) const noexcept {
    const auto found = m_chunks.find(pos);
    return found != m_chunks.end() ? found->second.get() : nullptr;
}

const Section* World::sectionAt(SectionPos pos) const noexcept {
    const Chunk* chunk = find(ChunkPos{pos.x, pos.z});
    return chunk != nullptr ? chunk->sectionAt(pos.y) : nullptr;
}

BlockId World::blockAt(BlockPos pos) const noexcept {
    if (!isValidWorldY(pos.y)) {
        return kAirBlock;
    }

    const Section* section = sectionAt(toSectionPos(pos));
    if (section == nullptr) {
        return kAirBlock;
    }
    return section->get(blockToLocalCoord(pos.x),
                        blockToLocalCoord(pos.y),
                        blockToLocalCoord(pos.z));
}

SectionNeighbourhood World::neighbourhood(SectionPos center) const {
    MC_PROFILE_SCOPE_N("World::neighbourhood");

    SectionNeighbourhood result;

    // Nine column lookups rather than 27: the vertical neighbours of a section
    // live in the same column, so the hash lookup is shared down the y axis.
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            const Chunk* chunk = find(ChunkPos{center.x + dx, center.z + dz});
            if (chunk == nullptr) {
                continue; // Entries stay null, which reads as air.
            }
            for (i32 dy = -1; dy <= 1; ++dy) {
                result.set(dx, dy, dz, chunk->sectionAt(center.y + dy));
            }
        }
    }

    return result;
}

usize World::memoryUsage() const {
    usize total = 0;
    for (const auto& [pos, chunk] : m_chunks) {
        total += chunk->memoryUsage();
    }
    return total;
}

} // namespace mc
