#include "world/World.hpp"

#include "core/Profile.hpp"
#include "world/SkyLight.hpp"

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

    const Chunk* chunk = find(toChunkPos(pos));
    // Not `sectionAt`: a column a worker is still generating must read as air, not
    // as whatever half-written palette it currently holds. ThreadSanitizer caught
    // this as `Palette::get` racing `Palette::fill`, and it is a real race rather
    // than a benign one -- `Palette::fill` frees the index vector, so a reader can
    // be walking memory that has just been returned to the allocator.
    //
    // Reading as air is also the answer every caller already handles: walking and
    // the benchmark camera both treat "nothing solid below" as a column that has not
    // arrived yet and hold their height, and the aim ray simply finds nothing. The
    // acquire load in `state()` pairs with the release store the generator ends on,
    // so observing Ready means observing every voxel it wrote.
    if (chunk == nullptr || chunk->state() != ChunkState::Ready) {
        return kAirBlock;
    }

    const Section* section = chunk->sectionAt(blockToSectionCoord(pos.y));
    if (section == nullptr) {
        return kAirBlock;
    }
    return section->get(blockToLocalCoord(pos.x),
                        blockToLocalCoord(pos.y),
                        blockToLocalCoord(pos.z));
}

namespace {

/// Marks section `sectionY` dirty in `chunk`, if that section exists.
void dirtySection(Chunk* chunk, i32 sectionY) {
    if (chunk == nullptr || !isValidSectionY(sectionY)) {
        return;
    }
    chunk->markSectionDirty(static_cast<usize>(sectionIndexInColumn(sectionY)));
}

} // namespace

World::EditStatus World::setBlock(BlockPos pos, BlockId block) {
    MC_PROFILE_SCOPE_N("World::setBlock");

    if (!isValidWorldY(pos.y)) {
        return EditStatus::OutsideWorld;
    }

    Chunk* chunk = find(toChunkPos(pos));
    if (chunk == nullptr || chunk->state() != ChunkState::Ready) {
        return EditStatus::NotLoaded;
    }
    // Checked after Ready and before any write. See the header: this is the whole
    // safety argument for editing without a lock.
    if (chunk->pinned()) {
        return EditStatus::Busy;
    }

    const i32 sectionY = blockToSectionCoord(pos.y);
    Section* section = chunk->sectionAt(sectionY);
    if (section == nullptr) {
        return EditStatus::OutsideWorld;
    }

    const i32 lx = blockToLocalCoord(pos.x);
    const i32 ly = blockToLocalCoord(pos.y);
    const i32 lz = blockToLocalCoord(pos.z);

    if (section->get(lx, ly, lz) == block) {
        return EditStatus::Unchanged;
    }
    section->set(lx, ly, lz, block);

    // (1) and (2): the section, plus every section the block touches. An axis
    // contributes a neighbour only when the block sits against that section wall,
    // so this is one section in the interior and at most eight in a corner.
    const i32 loX = lx == 0 ? -1 : 0;
    const i32 hiX = lx == kSectionSize - 1 ? 1 : 0;
    const i32 loY = ly == 0 ? -1 : 0;
    const i32 hiY = ly == kSectionSize - 1 ? 1 : 0;
    const i32 loZ = lz == 0 ? -1 : 0;
    const i32 hiZ = lz == kSectionSize - 1 ? 1 : 0;

    const ChunkPos column = chunk->position();
    for (i32 dz = loZ; dz <= hiZ; ++dz) {
        for (i32 dx = loX; dx <= hiX; ++dx) {
            Chunk* neighbour = (dx == 0 && dz == 0)
                                   ? chunk
                                   : find(ChunkPos{column.x + dx, column.z + dz});
            for (i32 dy = loY; dy <= hiY; ++dy) {
                dirtySection(neighbour, sectionY + dy);
            }
        }
    }

    // (3) Light. Recomputed for the whole column rather than incrementally: the
    // vertical fill depends on the column's heightmap, which one block can move by
    // any amount, and a full recompute measures about 0.5 ms -- per click, on a
    // path that is not the frame loop. An incremental relight is the optimisation
    // to reach for if that ever shows up in a profile, not before.
    const u16 lightChanged = computeSkyLight(*chunk);
    if (lightChanged == 0) {
        return EditStatus::Applied;
    }

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        if ((lightChanged & (1u << index)) == 0) {
            continue;
        }
        const i32 movedY = kMinSectionY + static_cast<i32>(index);
        chunk->markSectionDirty(index);

        // The neighbours' boundary faces are lit by light that lives in this column,
        // so they are stale too. Only the eight around it: light is column-local, so
        // nothing further out can be reading it.
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    continue;
                }
                dirtySection(find(ChunkPos{column.x + dx, column.z + dz}), movedY);
            }
        }
    }

    return EditStatus::Applied;
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

World::HeldCounts World::heldCounts(ChunkPos center) const {
    HeldCounts counts;
    for (const auto& [pos, chunk] : m_chunks) {
        if (chunk->pinned()) {
            ++counts.pinned;
        }
        if (chunk->state() == ChunkState::Generating) {
            ++counts.generating;
        }
        if (!isInRegion(pos, center)) {
            ++counts.outsideRegion;
        }
    }
    return counts;
}

usize World::memoryUsage() const {
    usize total = 0;
    for (const auto& [pos, chunk] : m_chunks) {
        total += chunk->memoryUsage();
    }
    return total;
}

} // namespace mc
