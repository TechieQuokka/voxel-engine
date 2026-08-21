#include "world/World.hpp"

#include "core/Profile.hpp"
#include "world/BlockLight.hpp"
#include "world/BlockTable.hpp"
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
    // Moved out of the map rather than erased, so the caller gets a chance to save
    // them before they go. See LoadResult::unloaded.
    result.unloaded.reserve(dropping.size());
    for (const ChunkPos pos : dropping) {
        const auto found = m_chunks.find(pos);
        MC_ASSERT(found != m_chunks.end());
        result.unloaded.push_back(std::move(found->second));
        m_chunks.erase(found);
    }

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

bool World::isReadyAt(BlockPos pos) const noexcept {
    if (!isValidWorldY(pos.y)) {
        return false;
    }
    const Chunk* chunk = find(toChunkPos(pos));
    // The same acquire load `blockAt` relies on, and the same pairing: observing
    // Ready means observing every voxel the generator wrote. A missing section is
    // still Ready -- it is genuinely air, uniformly, and reading it is safe.
    return chunk != nullptr && chunk->state() == ChunkState::Ready;
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

void World::dirtyAround(const LightTouch& touched) {
    // Each moved section, plus the twenty-six around it.
    //
    // **The ring is not caution, it is the padded grid.** A section's mesh is built
    // over a 34-cube that reaches one voxel into every neighbour, light included, so
    // a cell that changed on a section wall is read by the section on the other side
    // of it -- and by the diagonal ones too, because smooth lighting averages four
    // cells per corner. Dirtying only what moved would leave a seam of stale
    // brightness exactly along the section boundaries, which is the most visible
    // place it could possibly be.
    //
    // A torch spans at most two sections on each axis, so this is a few dozen marks
    // on a path that runs once per click. `markSectionDirty` is idempotent, so the
    // overlap between neighbouring entries costs nothing but the lookup.
    for (const SectionPos& moved : touched.sections) {
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                Chunk* column = find(ChunkPos{moved.x + dx, moved.z + dz});
                if (column == nullptr) {
                    continue;
                }
                for (i32 dy = -1; dy <= 1; ++dy) {
                    dirtySection(column, moved.y + dy);
                }
            }
        }
    }
}

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

    const BlockId previous = section->get(lx, ly, lz);
    if (previous == block) {
        return EditStatus::Unchanged;
    }

    // **The block light flood writes into as many as nine columns, so all nine have
    // to be checked, not just this one.** Sky light never leaves its column and the
    // pin above covers it; block light reaches fifteen blocks across a thirty-two
    // wide column, so it crosses a wall from almost anywhere. Writing into a pinned
    // neighbour is the same use-after-free the pin exists to prevent -- a mesher is
    // reading its `LightArray`, and raising a cell out of the uniform state
    // reallocates it.
    //
    // Asked only when there is light in reach to move, which in a world with no
    // torches in it is never. See `blockLightCanMove`.
    const bool lightMoves = blockLightCanMove(*this, pos, previous, block);
    if (lightMoves) {
        const ChunkPos here = chunk->position();
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                const Chunk* neighbour = find(ChunkPos{here.x + dx, here.z + dz});
                if (neighbour != nullptr && neighbour->pinned()) {
                    return EditStatus::Busy;
                }
            }
        }
    }

    section->set(lx, ly, lz, block);

    // Sticky, and never cleared when the last torch in a column is broken. Clearing
    // it would mean proving no other section holds one, and being wrong in that
    // direction loses light; being wrong in this one costs a flood that finds
    // nothing. See `Chunk::hasEmitter`.
    if (isEmitter(block)) {
        chunk->markHasEmitter();
    }

    // From here the column is no longer what the generator would produce, so it has
    // to survive being unloaded. Set after the `Unchanged` return above: placing a
    // block where that block already is must not make a column worth writing.
    chunk->markEdited();

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

    // (3) Block light, which is incremental and crosses columns.
    //
    // **The opposite choice from sky light below, and for a reason that is about
    // correctness rather than speed.** Sky light is recomputed wholesale because one
    // block can move a column's heightmap by any amount; block light cannot be
    // recomputed that way at all, because a torch's fifteen-block reach does not fit
    // inside the thirty-two-wide column the whole-column pass is built around. So it
    // floods outward through the loaded set instead, and reports the sections it
    // moved as positions rather than as a mask over one column.
    if (lightMoves) {
        LightTouch touched;
        updateBlockLight(*this, pos, previous, block, touched);
        dirtyAround(touched);
    }

    // (4) Sky light. Recomputed for the whole column rather than incrementally: the
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
