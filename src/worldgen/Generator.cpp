#include "worldgen/Generator.hpp"

#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace mc {
namespace {

/// Solid where density is positive. Minecraft's rule exactly.
constexpr bool isSolid(f32 density) {
    return density > 0.0f;
}

} // namespace

Generator::Generator(u32 seed) : m_seed(seed), m_graph(std::make_unique<DensityGraph>(seed)) {}

Generator::~Generator() = default;

i32 Generator::terrainHeight(i32 worldX, i32 worldZ) const {
    const ChunkPos columnPos = toChunkPos(BlockPos{worldX, 0, worldZ});

    static thread_local DensityGraph::Climate climate;
    static thread_local DensityField density;
    m_graph->fillColumn(columnPos, climate, density);

    const i32 localX = blockToLocalCoord(worldX);
    const i32 localZ = blockToLocalCoord(worldZ);

    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        if (isSolid(density.sample(localX, y, localZ))) {
            return y;
        }
    }
    return kWorldMinY - 1;
}

i32 Generator::surfaceHeight(i32 worldX, i32 worldZ) const {
    // Generates the column and reads the result, rather than consulting the density
    // field directly.
    //
    // Reading the field was cheaper and became wrong the moment carvers existed: thin
    // caves are cut per block, after the field, so the field's topmost solid voxel can
    // be air in the finished world. A helper that disagrees with the generator is worse
    // than a slow one -- the spawn point and every test would be reasoning about
    // terrain that does not exist.
    const ChunkPos columnPos = toChunkPos(BlockPos{worldX, 0, worldZ});

    // One-column cache, per thread. Callers sweep positions within a column, so this
    // turns a 32x32 sweep from 1,024 column generations into one.
    static thread_local std::unique_ptr<Chunk> cached;
    static thread_local const Generator* cachedOwner = nullptr;
    if (cached == nullptr || cached->position() != columnPos || cachedOwner != this) {
        cached = std::make_unique<Chunk>(columnPos);
        cachedOwner = this;
        generateColumn(*cached);
    }
    const Chunk& column = *cached;

    const i32 localX = blockToLocalCoord(worldX);
    const i32 localZ = blockToLocalCoord(worldZ);

    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        const Section* section = column.sectionAt(blockToSectionCoord(y));
        if (section == nullptr) {
            continue;
        }
        if (section->get(localX, blockToLocalCoord(y), localZ) != kAirBlock) {
            return y;
        }
    }
    return kWorldMinY - 1;
}

void Generator::generateColumn(Chunk& chunk) const {
    MC_PROFILE_SCOPE_N("Generator::generateColumn");

    // ~16 KiB of density plus 1 KiB of climate, per worker rather than per call.
    static thread_local DensityGraph::Climate climate;
    static thread_local DensityField density;

    m_graph->fillColumn(chunk.position(), climate, density);

    // ---------------------------------------------------------------------------
    // Stage 1: noise. Solid or air, and nothing else -- every solid block is stone.
    //
    // Sections are examined one at a time so the uniform cases can be taken whole:
    // a section that came out entirely air or entirely stone stores one palette entry
    // and no index array, which is what makes 12 sections per column affordable
    // (DESIGN.md 3.5). Most of a column is one or the other.
    // ---------------------------------------------------------------------------
    std::array<bool, Chunk::kSectionCount> sectionHasSolid{};

    // The terrain surface according to the density field alone, before carvers touch
    // anything. The surface rule needs it to tell an outdoor surface from a cave
    // ceiling: both are the top of a solid run, and without this every cave roof below
    // the beach level was being turned into sand.
    static thread_local std::array<i32, kSectionSize * kSectionSize> terrainTop;
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            i32 top = kWorldMinY - 1;
            for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
                if (isSolid(density.sample(x, y, z))) {
                    top = y;
                    break;
                }
            }
            terrainTop[static_cast<usize>(z * kSectionSize + x)] = top;
        }
    }

    // Solidity for one section at a time, so the carver can edit it before it is
    // committed to the palette. Writing to the palette and then carving would mean
    // repacking a section twice.
    static thread_local std::vector<u8> solid;
    solid.resize(kSectionVolume);

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        Section& section = chunk.sectionByIndex(index);
        const i32 sectionIndex = static_cast<i32>(index);
        const i32 sectionMinY = sectionIndexToWorldY(sectionIndex);
        const i32 sectionY = sectionMinY / kSectionSize;

        bool anyAir = false;
        bool anySolid = false;
        for (i32 localY = 0; localY < kSectionSize; ++localY) {
            const i32 worldY = sectionMinY + localY;
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    const bool isRock = isSolid(density.sample(x, worldY, z));
                    solid[localIndex(x, localY, z)] = isRock ? u8{1} : u8{0};
                    anySolid = anySolid || isRock;
                    anyAir = anyAir || !isRock;
                }
            }
        }

        // -----------------------------------------------------------------------
        // Stage 1b: carvers. Thin caves are carved per block, because a one-to-five
        // block tunnel cannot survive the 4x8x4 interpolation grid -- Minecraft has
        // the same constraint and answers it the same way. Skipped entirely for
        // sections that are all air, all rock outside the cave band, or above it.
        // -----------------------------------------------------------------------
        if (anySolid && DensityGraph::thinCavesReach(sectionY)) {
            m_graph->carveThinCaves(SectionPos{chunk.position().x, sectionY, chunk.position().z},
                                    solid);
            // The carve can only add air, so re-derive just that half.
            anyAir = false;
            for (const u8 value : solid) {
                if (value == 0) {
                    anyAir = true;
                    break;
                }
            }
        }

        sectionHasSolid[index] = anySolid;

        if (!anySolid) {
            section.fill(kAirBlock);
            continue;
        }
        if (!anyAir) {
            section.fill(kStoneBlock);
            continue;
        }

        section.fill(kAirBlock);
        for (i32 localY = 0; localY < kSectionSize; ++localY) {
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    if (solid[localIndex(x, localY, z)] != 0) {
                        section.set(x, localY, z, kStoneBlock);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Stage 2: surface. Walk each column down from the sky and replace the top of
    // each solid run with soil.
    //
    // Down from the top rather than up from a known height, because with a 3D
    // density field there is no single surface: an overhang has a top face partway
    // down, and it should be grassed too. Minecraft's surface rules work the same
    // way, on runs rather than on a heightmap.
    // ---------------------------------------------------------------------------
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            // Depth below the most recent air block. -1 means "still in open air".
            i32 depth = -1;

            // Only decorate near the terrain surface. Deeper solid runs are cave
            // ceilings, and grassing those was both wrong and expensive -- it turned
            // every cave roof under y=66 into sand.
            const i32 top = terrainTop[static_cast<usize>(z * kSectionSize + x)];
            const i32 decorateBelow = top + 1;
            const i32 decorateAbove = top - kSurfaceBand;

            for (i32 worldY = kWorldMaxY - 1; worldY >= kWorldMinY; --worldY) {
                const usize sectionIndex =
                    static_cast<usize>(sectionIndexInColumn(blockToSectionCoord(worldY)));

                // Skip whole uniform sections rather than reading 32 voxels of known
                // answer. An all-air section resets the run; an all-stone one is
                // interior and only advances the depth counter.
                if (!sectionHasSolid[sectionIndex]) {
                    depth = -1;
                    worldY = sectionIndexToWorldY(static_cast<i32>(sectionIndex));
                    continue;
                }

                Section& section = chunk.sectionByIndex(sectionIndex);
                const i32 localY = blockToLocalCoord(worldY);

                if (section.get(x, localY, z) == kAirBlock) {
                    depth = -1;
                    continue;
                }

                ++depth;
                if (depth >= kSoilDepth) {
                    continue; // Interior stone.
                }
                if (worldY > decorateBelow || worldY < decorateAbove) {
                    continue; // A cave ceiling, not the world's surface.
                }

                const bool beach = worldY <= kBeachLevel;
                BlockId block = kStoneBlock;
                if (depth == 0) {
                    block = beach ? kSandBlock : kGrassBlock;
                } else {
                    block = beach ? kSandBlock : kDirtBlock;
                }
                section.set(x, localY, z, block);
            }
        }
    }

    chunk.markAllDirty();
    chunk.setState(ChunkState::Ready);
}

} // namespace mc
