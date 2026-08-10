#include "worldgen/Generator.hpp"

#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <algorithm>
#include <array>

namespace mc {
namespace {

/// Solid where density is positive. Minecraft's rule exactly.
constexpr bool isSolid(f32 density) {
    return density > 0.0f;
}

} // namespace

Generator::Generator(u32 seed) : m_seed(seed), m_graph(std::make_unique<DensityGraph>(seed)) {}

Generator::~Generator() = default;

i32 Generator::surfaceHeight(i32 worldX, i32 worldZ) const {
    const ChunkPos column = toChunkPos(BlockPos{worldX, 0, worldZ});

    // thread_local because these are ~16 KiB each and this may be called in a loop.
    static thread_local DensityGraph::Climate climate;
    static thread_local DensityField density;
    m_graph->fillColumn(column, climate, density);

    const i32 localX = blockToLocalCoord(worldX);
    const i32 localZ = blockToLocalCoord(worldZ);

    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        if (isSolid(density.sample(localX, y, localZ))) {
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
    std::array<bool, Chunk::kSectionCount> sectionHasAir{};
    std::array<bool, Chunk::kSectionCount> sectionHasSolid{};

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        Section& section = chunk.sectionByIndex(index);
        const i32 sectionMinY = sectionIndexToWorldY(static_cast<i32>(index));

        // First pass over the section: does it need an index array at all?
        bool anyAir = false;
        bool anySolid = false;
        for (i32 localY = 0; localY < kSectionSize && !(anyAir && anySolid); ++localY) {
            for (i32 z = 0; z < kSectionSize && !(anyAir && anySolid); ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    if (isSolid(density.sample(x, sectionMinY + localY, z))) {
                        anySolid = true;
                    } else {
                        anyAir = true;
                    }
                    if (anyAir && anySolid) {
                        break;
                    }
                }
            }
        }

        sectionHasAir[index] = anyAir;
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
            const i32 worldY = sectionMinY + localY;
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    if (isSolid(density.sample(x, worldY, z))) {
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
