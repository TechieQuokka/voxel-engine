#include "world/BlockLight.hpp"

#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

namespace mc {
namespace {

/// The six face neighbours, in the order the passes walk them.
constexpr std::array<BlockPos, 6> kNeighbours{{
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1},
}};

constexpr BlockPos offsetBy(BlockPos pos, BlockPos delta) {
    return {pos.x + delta.x, pos.y + delta.y, pos.z + delta.z};
}

/// A section lookup that remembers the last one it found.
///
/// **The cache is not a micro-optimisation, it is what keeps the flood affordable.**
/// A torch in open air settles some four thousand cells, and every one of them reads
/// a block and reads or writes a light level. Going through the chunk map for each
/// would be two hash lookups per cell. The passes below walk outward from a point, so
/// consecutive cells are overwhelmingly in the section the last one was in, and one
/// remembered pointer turns almost all of it into arithmetic.
class Cursor {
public:
    explicit Cursor(World& world) : m_world(world) {}

    /// Null when the column is not loaded, has not finished generating, or `y` is
    /// outside the world. All three read as opaque below -- see the header.
    Section* sectionFor(BlockPos pos) {
        const i32 sectionY = blockToSectionCoord(pos.y);
        const ChunkPos column{blockToSectionCoord(pos.x), blockToSectionCoord(pos.z)};

        if (m_section != nullptr && column == m_column && sectionY == m_sectionY) {
            return m_section;
        }
        if (!isValidSectionY(sectionY)) {
            return nullptr;
        }

        if (m_chunk == nullptr || column != m_column) {
            m_chunk = m_world.find(column);
            m_column = column;
            m_section = nullptr;
            if (m_chunk != nullptr && m_chunk->state() != ChunkState::Ready) {
                m_chunk = nullptr;
            }
        }
        if (m_chunk == nullptr) {
            return nullptr;
        }

        m_sectionY = sectionY;
        m_section = m_chunk->sectionAt(sectionY);
        return m_section;
    }

    BlockId blockAt(BlockPos pos) {
        const Section* section = sectionFor(pos);
        if (section == nullptr) {
            return kAirBlock;
        }
        return section->get(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                            blockToLocalCoord(pos.z));
    }

    /// Whether light is stopped here. **Unloaded reads as blocked, not as air**, so a
    /// flood stops at the edge of the loaded region instead of pouring into a column
    /// that has not arrived and cannot be written to.
    bool blocked(BlockPos pos) {
        const Section* section = sectionFor(pos);
        if (section == nullptr) {
            return true;
        }
        return kBlocks[section->get(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                                    blockToLocalCoord(pos.z))]
            .opaque;
    }

    u8 lightAt(BlockPos pos) {
        const Section* section = sectionFor(pos);
        if (section == nullptr) {
            return 0;
        }
        return section->blockLight(blockToLocalCoord(pos.x), blockToLocalCoord(pos.y),
                                   blockToLocalCoord(pos.z));
    }

    /// Writes, and records the section as touched if the value actually moved.
    void setLight(BlockPos pos, u8 level, LightTouch& touched) {
        Section* section = sectionFor(pos);
        if (section == nullptr) {
            return;
        }
        const i32 lx = blockToLocalCoord(pos.x);
        const i32 ly = blockToLocalCoord(pos.y);
        const i32 lz = blockToLocalCoord(pos.z);
        if (section->blockLight(lx, ly, lz) == level) {
            return;
        }
        section->setBlockLight(lx, ly, lz, level);
        note(touched, SectionPos{m_column.x, m_sectionY, m_column.z});
    }

private:
    /// Linear, because the list is short by construction: a flood of radius fifteen
    /// spans at most two sections on each axis, so eight, and a whole-column seed a
    /// few dozen. A set would cost more to build than this costs to scan.
    static void note(LightTouch& touched, SectionPos pos) {
        if (std::find(touched.sections.begin(), touched.sections.end(), pos)
            == touched.sections.end()) {
            touched.sections.push_back(pos);
        }
    }

    World& m_world;
    Chunk* m_chunk = nullptr;
    Section* m_section = nullptr;
    ChunkPos m_column{};
    i32 m_sectionY = 0;
};

/// True when any block type named in this section's palette emits light.
bool paletteHasEmitter(const Section& section) {
    const std::span<const BlockId> entries = section.storage().entries();
    return std::any_of(entries.begin(), entries.end(),
                       [](BlockId id) { return isEmitter(id); });
}

/// Bucket queues for the add pass, plus the removal queue.
///
/// One bucket per level so the flood settles every cell on its first write, exactly
/// as the sky light pass does: light only ever spreads to a strictly lower level, so
/// walking 15 down to 1 needs no priority queue and no revisiting.
struct Scratch {
    std::array<std::vector<BlockPos>, 16> add;
    /// Cells to darken, each with the level it held before it was cleared.
    std::vector<std::pair<BlockPos, u8>> remove;

    void clear() {
        for (auto& bucket : add) {
            bucket.clear();
        }
        remove.clear();
    }
};

Scratch& scratch() {
    static thread_local Scratch instance;
    instance.clear();
    return instance;
}

void seed(Scratch& s, BlockPos pos, u8 level) {
    if (level != 0) {
        s.add[static_cast<usize>(level)].push_back(pos);
    }
}

/// Darkens everything that was lit *by* the cell the removal started from, and
/// collects the survivors at the edge of the hole as seeds for the add pass.
///
/// The test that separates the two is one comparison. A neighbour dimmer than the
/// cell being darkened could only have got its light through that cell, so it goes
/// too. A neighbour as bright or brighter has its own path to some other source, so
/// it stays and becomes a seed -- and that is what stops breaking one torch from
/// blacking out the room a second torch is also lighting.
void removeFrom(Cursor& cursor, Scratch& s, BlockPos origin, u8 level,
                LightTouch& touched) {
    cursor.setLight(origin, 0, touched);
    s.remove.emplace_back(origin, level);

    for (usize i = 0; i < s.remove.size(); ++i) {
        const auto [pos, had] = s.remove[i];

        for (const BlockPos& delta : kNeighbours) {
            const BlockPos next = offsetBy(pos, delta);
            if (!isValidWorldY(next.y)) {
                continue;
            }

            const u8 there = cursor.lightAt(next);
            if (there == 0) {
                continue;
            }

            if (there < had) {
                // Lit through the cell that just went dark, so it goes dark too --
                // unless it makes its own light, in which case it keeps it and
                // becomes a seed instead.
                const u8 emits = luminanceOf(cursor.blockAt(next));
                if (emits != 0) {
                    cursor.setLight(next, emits, touched);
                    seed(s, next, emits);
                    continue;
                }
                cursor.setLight(next, 0, touched);
                s.remove.emplace_back(next, there);
            } else {
                // As bright or brighter: it has another source, and re-spreading
                // from it is what refills the hole.
                seed(s, next, there);
            }
        }
    }
}

/// Spreads every seeded cell outward, losing one level per block.
void addAll(Cursor& cursor, Scratch& s, LightTouch& touched) {
    // Down to 2, not to 1: a cell at level 1 has nothing left to give, so its bucket
    // would be walked only to find that out six times per entry.
    for (i32 level = kMaxLight; level >= 2; --level) {
        const auto give = static_cast<u8>(level - 1);
        auto& bucket = s.add[static_cast<usize>(level)];

        // Indexed rather than ranged, because spilling can push into this very
        // bucket -- a seed placed at `level` by the removal pass above.
        for (usize i = 0; i < bucket.size(); ++i) {
            const BlockPos pos = bucket[i];

            // A seed is only valid if the cell still holds what it was seeded with.
            // Both passes can raise a cell after it was queued, and the removal pass
            // can clear one.
            if (cursor.lightAt(pos) != level) {
                continue;
            }

            for (const BlockPos& delta : kNeighbours) {
                const BlockPos next = offsetBy(pos, delta);
                if (!isValidWorldY(next.y)) {
                    continue;
                }
                if (cursor.blocked(next) || cursor.lightAt(next) >= give) {
                    continue;
                }
                cursor.setLight(next, give, touched);
                s.add[static_cast<usize>(give)].push_back(next);
            }
        }
    }
}

} // namespace

void updateBlockLight(World& world, BlockPos pos, BlockId before, BlockId after,
                      LightTouch& touched) {
    const u8 wasEmitting = luminanceOf(before);
    const u8 nowEmitting = luminanceOf(after);
    const bool wasBlocking = kBlocks[before].opaque;
    const bool nowBlocking = kBlocks[after].opaque;

    // Dirt to gravel, stone to cobblestone: neither what the cell gives nor what it
    // lets through has moved, so no light anywhere can have changed. This is the
    // common case on the digging path and it costs one comparison.
    if (wasEmitting == nowEmitting && wasBlocking == nowBlocking) {
        return;
    }

    Cursor cursor(world);
    Scratch& s = scratch();

    // The cell may be holding light it is no longer entitled to: it stopped emitting,
    // or it stopped letting light through. Either way what it was giving out has to
    // be taken back before anything is added, or the add pass would simply find every
    // cell already bright enough and do nothing.
    const u8 held = cursor.lightAt(pos);
    if (held != 0 && (nowBlocking || nowEmitting < held)) {
        removeFrom(cursor, s, pos, held, touched);
    }

    if (nowEmitting != 0) {
        cursor.setLight(pos, nowEmitting, touched);
        seed(s, pos, nowEmitting);
    }

    // A wall came down. Light enters from whatever is already lit around the hole --
    // this is the case that makes a room brighten the moment it is opened.
    if (!nowBlocking) {
        for (const BlockPos& delta : kNeighbours) {
            const BlockPos next = offsetBy(pos, delta);
            if (isValidWorldY(next.y)) {
                seed(s, next, cursor.lightAt(next));
            }
        }
    }

    addAll(cursor, s, touched);
}

bool blockLightCanMove(const World& world, BlockPos pos, BlockId before, BlockId after) {
    if (luminanceOf(before) == luminanceOf(after)
        && kBlocks[before].opaque == kBlocks[after].opaque) {
        return false;
    }
    if (luminanceOf(after) != 0) {
        return true;
    }

    // A light level read straight through the const world, since there is no flood to
    // amortise a cursor over -- seven reads at most, and the first hit answers it.
    const auto lightAt = [&world](BlockPos at) -> u8 {
        if (!isValidWorldY(at.y)) {
            return 0;
        }
        const Chunk* chunk = world.find(toChunkPos(at));
        if (chunk == nullptr || chunk->state() != ChunkState::Ready) {
            return 0;
        }
        const Section* section = chunk->sectionAt(blockToSectionCoord(at.y));
        if (section == nullptr) {
            return 0;
        }
        return section->blockLight(blockToLocalCoord(at.x), blockToLocalCoord(at.y),
                                   blockToLocalCoord(at.z));
    };

    if (lightAt(pos) != 0) {
        return true;
    }
    for (const BlockPos& delta : kNeighbours) {
        if (lightAt(offsetBy(pos, delta)) != 0) {
            return true;
        }
    }
    return false;
}

void noteEmitters(Chunk& chunk) {
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        if (paletteHasEmitter(chunk.sectionByIndex(index))) {
            chunk.markHasEmitter();
            return;
        }
    }
}

void seedBlockLight(World& world, ChunkPos column, LightTouch& touched) {
    Cursor cursor(world);
    Scratch& s = scratch();

    // The eight neighbours as well as the column itself. A torch one block inside the
    // border owes light to the column next door, and which of the two loaded first is
    // not something either one can know.
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            const ChunkPos here{column.x + dx, column.z + dz};
            Chunk* chunk = world.find(here);
            if (chunk == nullptr || chunk->state() != ChunkState::Ready
                || !chunk->hasEmitter()) {
                continue;
            }

            for (usize index = 0; index < Chunk::kSectionCount; ++index) {
                const Section& section = chunk->sectionByIndex(index);

                // Asked of the palette rather than of the voxels, for the reason
                // `noteEmitters` above is asked at all: a torch that is not named in
                // the palette is not in the section, and a section is 32,768 voxels.
                if (!paletteHasEmitter(section)) {
                    continue;
                }

                const i32 baseY = sectionIndexToWorldY(static_cast<i32>(index));
                for (i32 y = 0; y < kSectionSize; ++y) {
                    for (i32 z = 0; z < kSectionSize; ++z) {
                        for (i32 x = 0; x < kSectionSize; ++x) {
                            const u8 emits = luminanceOf(section.get(x, y, z));
                            if (emits == 0) {
                                continue;
                            }
                            const BlockPos pos{here.x * kSectionSize + x, baseY + y,
                                               here.z * kSectionSize + z};
                            if (cursor.lightAt(pos) < emits) {
                                cursor.setLight(pos, emits, touched);
                            }
                            seed(s, pos, emits);
                        }
                    }
                }
            }
        }
    }

    addAll(cursor, s, touched);
}

} // namespace mc
