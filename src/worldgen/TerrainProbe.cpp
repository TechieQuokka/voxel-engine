#include "worldgen/TerrainProbe.hpp"

#include "core/Log.hpp"
#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "worldgen/DensityGraph.hpp"
#include "worldgen/Generator.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace mc {
namespace {

/// Counts, indexed by BlockId.
using Histogram = std::array<u64, kBlocks.size()>;

/// Air below the surface, which is the measure caves were tuned against.
///
/// "Below the surface" has to mean below *this voxel column's* own surface, not
/// below some fixed height. Counting a fixed band instead sweeps in the open sky
/// above low terrain and reports it as cave: the first version of this probe did
/// exactly that and read 21 % where the real figure is under 7 %.
struct AirBelowSurface {
    u64 air = 0;
    u64 all = 0;
};

void countAirBelowSurface(const Chunk& column, AirBelowSurface& out) {
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            bool belowSurface = false;
            for (i32 worldY = kWorldMaxY - 1; worldY >= kWorldMinY; --worldY) {
                const Section* section = column.sectionAt(blockToSectionCoord(worldY));
                if (section == nullptr) {
                    continue;
                }
                const BlockId block = section->get(x, blockToLocalCoord(worldY), z);

                if (!belowSurface) {
                    // The first solid block from the sky down is the surface; what
                    // is under it is what caves get to hollow out.
                    belowSurface = block != kAirBlock;
                    continue;
                }
                ++out.all;
                if (block == kAirBlock) {
                    ++out.air;
                }
            }
        }
    }
}

void countColumn(const Chunk& column, Histogram& total, std::vector<Histogram>& byY) {
    for (i32 worldY = kWorldMinY; worldY < kWorldMaxY; ++worldY) {
        const Section* section = column.sectionAt(blockToSectionCoord(worldY));
        if (section == nullptr) {
            continue;
        }
        const i32 localY = blockToLocalCoord(worldY);
        Histogram& row = byY[static_cast<usize>(worldY - kWorldMinY)];

        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                const BlockId block = section->get(x, localY, z);
                ++total[block];
                ++row[block];
            }
        }
    }
}

f64 percentOf(u64 part, u64 whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<f64>(part) / static_cast<f64>(whole);
}

void reportComposition(const Histogram& total, u64 voxels) {
    logInfo("composition, share of all {} voxels:", voxels);

    // Sorted by count, because the interesting line is always "which of these did I
    // barely place at all" and a table in table order buries it.
    std::vector<usize> order(kBlocks.size());
    for (usize i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&total](usize a, usize b) { return total[a] > total[b]; });

    for (const usize id : order) {
        if (total[id] == 0) {
            logInfo("  {:<12} {:>10} {:>9}   <- never placed", kBlocks[id].name, 0, "-");
            continue;
        }
        logInfo("  {:<12} {:>10} {:>8.4f} %", kBlocks[id].name, total[id],
                percentOf(total[id], voxels));
    }
}

/// The measure caves were tuned against, restated here so it stays comparable.
void reportUndergroundAir(const AirBelowSurface& counts) {
    logInfo("underground air fraction: {:.2f} %  (tuned to 6.8 %; Minecraft sits at 3-8 %)",
            percentOf(counts.air, counts.all));
}

/// The deepslate gradient, one line per Y. This is the check that a transition is a
/// transition and not a threshold: a flat cut shows up as 0 % on one line and 100 %
/// on the next.
void reportStoneGradient(const std::vector<Histogram>& byY) {
    logInfo("stone / deepslate / bedrock share of solid rock, by height:");
    for (i32 y = 16; y >= kWorldMinY; --y) {
        const Histogram& row = byY[static_cast<usize>(y - kWorldMinY)];
        const u64 stone = row[kStoneBlock];
        const u64 deep = row[kDeepslateBlock];
        const u64 rock = row[kBedrockBlock];
        const u64 sum = stone + deep + rock;
        if (sum == 0) {
            continue;
        }
        // Dense near the transition, sparse away from it.
        const bool interesting = (y <= 12 && y >= -4) || (y % 8 == 0);
        if (!interesting) {
            continue;
        }
        logInfo("   y {:>4}   stone {:>6.1f}   deepslate {:>6.1f}   bedrock {:>6.1f}",
                y, percentOf(stone, sum), percentOf(deep, sum), percentOf(rock, sum));
    }
}

void reportCrossSection(const Chunk& column, i32 minY, i32 maxY) {
    constexpr i32 kSliceZ = kSectionSize / 2;

    logInfo("cross-section of column {},{} at z={}, x running 0..{}:",
            column.position().x, column.position().z, kSliceZ, kSectionSize - 1);

    for (i32 y = maxY; y >= minY; --y) {
        const Section* section = column.sectionAt(blockToSectionCoord(y));
        if (section == nullptr) {
            continue;
        }
        const i32 localY = blockToLocalCoord(y);

        std::string row;
        row.reserve(static_cast<usize>(kSectionSize));
        for (i32 x = 0; x < kSectionSize; ++x) {
            row.push_back(kBlocks[section->get(x, localY, kSliceZ)].glyph);
        }
        logInfo("   y {:>4} |{}|", y, row);
    }
}

} // namespace

void runTerrainProbe(const Generator& generator, const ProbeOptions& options) {
    const i32 columns = std::max(1, options.columns);

    Histogram total{};
    std::vector<Histogram> byY(static_cast<usize>(kWorldHeight));

    AirBelowSurface undergroundAir;

    // A diagonal sweep rather than a block of adjacent columns: continentalness runs
    // at 0.00035 cycles per block, so a compact patch would measure one landform and
    // report it as the world.
    for (i32 i = 0; i < columns; ++i) {
        Chunk column(ChunkPos{i * 3, i * 5});
        generator.generateColumn(column);
        countColumn(column, total, byY);
        countAirBelowSurface(column, undergroundAir);
    }

    const auto voxels = static_cast<u64>(columns) * static_cast<u64>(kSectionVolume)
                        * static_cast<u64>(kSectionsPerColumn);

    logInfo("--- terrain probe: {} columns, {} voxels, seed {} ---",
            columns, voxels, generator.seed());
    reportComposition(total, voxels);
    reportUndergroundAir(undergroundAir);
    reportStoneGradient(byY);

    if (options.crossSection) {
        // Regenerated rather than kept from the loop: a column is 400 KiB of section
        // storage and the summary above is what the caller reads first anyway.
        Chunk sample(ChunkPos{0, 0});
        generator.generateColumn(sample);
        reportCrossSection(sample, options.sliceMinY, options.sliceMaxY);
    }
    logInfo("-----------------------------------------------------");
}

} // namespace mc
