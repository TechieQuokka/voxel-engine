#include "world/SkyLight.hpp"

#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace mc {
namespace {

constexpr u8 kFullLight = 15;
constexpr i32 kColumnArea = kSectionSize * kSectionSize;
constexpr usize kColumnVolume =
    static_cast<usize>(kColumnArea) * static_cast<usize>(kWorldHeight);

/// Flat buffer index. Y major, then z, then x -- so a horizontal slice is
/// contiguous, which is the order both fill passes below walk in.
constexpr usize lightIndex(i32 x, i32 y, i32 z) {
    return (static_cast<usize>(y - kWorldMinY) * static_cast<usize>(kSectionSize)
            + static_cast<usize>(z))
               * static_cast<usize>(kSectionSize)
           + static_cast<usize>(x);
}

constexpr usize columnIndex(i32 x, i32 z) {
    return static_cast<usize>(z) * static_cast<usize>(kSectionSize) + static_cast<usize>(x);
}

/// Scratch, per worker. Two buffers of 393,216 bytes; allocating them per call
/// would cost more than the work they hold.
struct Scratch {
    std::vector<u8> light;
    std::vector<u8> opaque;
    /// Highest opaque block per (x, z), or kWorldMinY - 1 for a column of pure air.
    std::array<i32, kColumnArea> top{};
    /// Bucket queues, one per level. Light only ever propagates to a strictly
    /// lower level, so walking 15 down to 1 settles every cell on first write and
    /// no priority queue is needed.
    std::array<std::vector<u32>, 16> buckets;

    /// Per-section summary, so the store pass needs no second scan of the column.
    std::array<u32, Chunk::kSectionCount> written{};
    std::array<u8, Chunk::kSectionCount> minLevel{};
    std::array<u8, Chunk::kSectionCount> maxLevel{};
};

Scratch& scratch() {
    static thread_local Scratch instance;
    instance.light.assign(kColumnVolume, 0);
    instance.opaque.assign(kColumnVolume, 0);
    for (auto& bucket : instance.buckets) {
        bucket.clear();
    }
    instance.written.fill(0);
    instance.minLevel.fill(kFullLight);
    instance.maxLevel.fill(0);
    return instance;
}

void note(Scratch& s, i32 worldY, u8 level) {
    const auto section = static_cast<usize>(sectionIndexInColumn(blockToSectionCoord(worldY)));
    ++s.written[section];
    s.minLevel[section] = std::min(s.minLevel[section], level);
    s.maxLevel[section] = std::max(s.maxLevel[section], level);
}

} // namespace

u16 computeSkyLight(Chunk& chunk) {
    Scratch& s = scratch();

    // ------------------------------------------------------------------------
    // Opacity, read once. A uniform section is one branch rather than 32,768
    // palette lookups, and most of a column is uniform either way.
    // ------------------------------------------------------------------------
    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        const Section& section = chunk.sectionByIndex(index);
        const i32 baseY = sectionIndexToWorldY(static_cast<i32>(index));

        if (section.isUniform()) {
            if (!kBlocks[section.uniformBlock()].opaque) {
                continue; // Already zero.
            }
            std::fill_n(s.opaque.begin() + static_cast<std::ptrdiff_t>(lightIndex(0, baseY, 0)),
                        static_cast<std::ptrdiff_t>(kColumnArea) * kSectionSize, u8{1});
            continue;
        }

        for (i32 y = 0; y < kSectionSize; ++y) {
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    s.opaque[lightIndex(x, baseY + y, z)] =
                        kBlocks[section.get(x, y, z)].opaque ? u8{1} : u8{0};
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Heightmap, then the vertical fill: straight down from the sky at full
    // strength until something stops it.
    //
    // Exact, and dependent on nothing but this column -- which is why open sky, the
    // surface and everything above it are untouched by the seam in the spread pass
    // below. Walked y-major so each slice is a contiguous run.
    // ------------------------------------------------------------------------
    s.top.fill(kWorldMinY - 1);
    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                const usize column = columnIndex(x, z);
                if (s.top[column] != kWorldMinY - 1) {
                    continue; // Already blocked higher up.
                }
                const usize index = lightIndex(x, y, z);
                if (s.opaque[index] != 0) {
                    s.top[column] = y;
                    continue;
                }
                s.light[index] = kFullLight;
                note(s, y, kFullLight);
            }
        }
    }

    // ------------------------------------------------------------------------
    // Seed the spread from the daylit cells that actually border darkness.
    //
    // Seeding every lit cell would mean a quarter of a million entries per column,
    // nearly all of them surrounded by cells already at 15 with nothing to give. A
    // lit cell at (x, y, z) can only spill somewhere new if a horizontal neighbour
    // is blocked at or above y -- so the seeds are exactly the cells sitting in the
    // step between this column's terrain height and a taller neighbour's.
    // ------------------------------------------------------------------------
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            const i32 here = s.top[columnIndex(x, z)];

            i32 tallest = here;
            if (x > 0) { tallest = std::max(tallest, s.top[columnIndex(x - 1, z)]); }
            if (x + 1 < kSectionSize) { tallest = std::max(tallest, s.top[columnIndex(x + 1, z)]); }
            if (z > 0) { tallest = std::max(tallest, s.top[columnIndex(x, z - 1)]); }
            if (z + 1 < kSectionSize) { tallest = std::max(tallest, s.top[columnIndex(x, z + 1)]); }

            for (i32 y = here + 1; y <= tallest; ++y) {
                if (!isValidWorldY(y)) {
                    continue;
                }
                s.buckets[kFullLight].push_back(static_cast<u32>(lightIndex(x, y, z)));
            }
        }
    }

    // ------------------------------------------------------------------------
    // Spread sideways and up, losing a level per block. Brightest bucket first, so
    // a cell is settled the first time it is written and never revisited.
    // ------------------------------------------------------------------------
    for (i32 level = kFullLight; level >= 1; --level) {
        const auto give = static_cast<u8>(level - 1);
        auto& bucket = s.buckets[static_cast<usize>(level)];

        for (usize i = 0; i < bucket.size(); ++i) {
            const u32 index = bucket[i];
            if (s.light[index] != level) {
                continue; // A brighter path reached it first.
            }

            const auto flat = static_cast<i32>(index);
            const i32 x = flat % kSectionSize;
            const i32 z = (flat / kSectionSize) % kSectionSize;
            const i32 y = flat / kColumnArea + kWorldMinY;

            const auto spill = [&](i32 nx, i32 ny, i32 nz, u8 amount) {
                if (amount == 0) {
                    return;
                }
                if (nx < 0 || nx >= kSectionSize || nz < 0 || nz >= kSectionSize) {
                    return; // The documented seam: light stops at the column wall.
                }
                if (!isValidWorldY(ny)) {
                    return;
                }
                const usize target = lightIndex(nx, ny, nz);
                if (s.opaque[target] != 0 || s.light[target] >= amount) {
                    return;
                }
                s.light[target] = amount;
                note(s, ny, amount);
                s.buckets[amount].push_back(static_cast<u32>(target));
            };

            spill(x - 1, y, z, give);
            spill(x + 1, y, z, give);
            spill(x, y, z - 1, give);
            spill(x, y, z + 1, give);
            spill(x, y + 1, z, give);
            // Downward keeps full strength, but only from full strength. That is
            // what a sunbeam through a hole in a cave roof is, and why it does not
            // dim with depth.
            spill(x, y - 1, z, level == kFullLight ? kFullLight : give);
        }
    }

    // ------------------------------------------------------------------------
    // Store, and report what moved. A section that came out all one level keeps no
    // array at all, which is the whole feasibility argument -- see LightArray.
    //
    // Which sections to *write* is decided from the counters the passes above kept,
    // not from a fresh scan: the scan would be another 393,216 reads per column to
    // learn what was already known while writing.
    //
    // Whether a write actually changed anything is a separate question, and one an
    // edit needs answered -- see the header. The two uniform cases answer it in O(1)
    // from the array's own uniform state. The mixed case compares as it writes,
    // which is why it no longer clears the array first: the old values are the thing
    // being compared against, so erasing them would destroy the answer.
    // ------------------------------------------------------------------------
    u16 changed = 0;

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        Section& section = chunk.sectionByIndex(index);
        LightArray& array = section.skyLightArray();
        const i32 baseY = sectionIndexToWorldY(static_cast<i32>(index));
        const auto bit = static_cast<u16>(1u << index);

        if (s.written[index] == 0) {
            if (!array.isUniform() || array.uniformLevel() != 0) {
                array.fill(0); // Never reached by daylight.
                changed |= bit;
            }
            continue;
        }
        if (s.written[index] == kSectionVolume && s.minLevel[index] == s.maxLevel[index]) {
            const u8 level = s.minLevel[index];
            if (!array.isUniform() || array.uniformLevel() != level) {
                array.fill(level);
                changed |= bit;
            }
            continue;
        }

        for (i32 y = 0; y < kSectionSize; ++y) {
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    const u8 level = s.light[lightIndex(x, baseY + y, z)];
                    if (section.skyLight(x, y, z) == level) {
                        continue;
                    }
                    section.setSkyLight(x, y, z, level);
                    changed |= bit;
                }
            }
        }
    }

    return changed;
}

} // namespace mc
