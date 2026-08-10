#include "worldgen/Generator.hpp"

#include "core/Profile.hpp"
#include "world/BlockRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mc {
namespace {

/// Deterministic scalar hash, for turning a seed into phase offsets.
u32 mix(u32 value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

} // namespace

Generator::Generator(u32 seed)
    : m_seed(seed),
      // Phase offsets rather than an amplitude or frequency change, so a different
      // seed is a different world instead of the same one scaled.
      m_offsetX(static_cast<f32>(mix(seed) % 100000u) * 0.01f),
      m_offsetZ(static_cast<f32>(mix(seed ^ 0x9E3779B9u) % 100000u) * 0.01f) {}

i32 Generator::surfaceHeight(i32 worldX, i32 worldZ) const {
    const f32 fx = static_cast<f32>(worldX) + m_offsetX;
    const f32 fz = static_cast<f32>(worldZ) + m_offsetZ;

    // Three octaves standing in for the continentalness / erosion / detail split
    // that 3.12 describes, so the shape of the terrain is roughly what Phase 4 will
    // produce and the streaming numbers stay comparable.
    const f32 continents = 18.0f * std::sin(fx * 0.0131f) * std::cos(fz * 0.0117f);
    const f32 hills = 7.0f * std::sin((fx + fz) * 0.0413f);
    const f32 detail = 2.5f * std::sin(fx * 0.113f) * std::sin(fz * 0.097f);

    const f32 height = static_cast<f32>(kBaseHeight) + continents + hills + detail;

    return std::clamp(static_cast<i32>(std::lround(height)), kWorldMinY + 1, kWorldMaxY - 1);
}

BlockId Generator::blockFor(i32 y, i32 surface) {
    if (y > surface) {
        return kAirBlock;
    }

    const bool beach = surface <= kBeachLevel;
    if (y == surface) {
        return beach ? kSandBlock : kGrassBlock;
    }
    if (y > surface - kSoilDepth) {
        return beach ? kSandBlock : kDirtBlock;
    }
    return kStoneBlock;
}

void Generator::generateColumn(Chunk& chunk) const {
    MC_PROFILE_SCOPE_N("Generator::generateColumn");

    const ChunkPos position = chunk.position();
    const i32 baseX = position.x * kSectionSize;
    const i32 baseZ = position.z * kSectionSize;

    // The surface is a 2D function, so it is evaluated once per column position and
    // shared by all 12 sections. This is exactly why the load unit is a column.
    std::array<i32, static_cast<usize>(kSectionSize) * kSectionSize> heights{};
    i32 minSurface = kWorldMaxY;
    i32 maxSurface = kWorldMinY;

    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            const i32 surface = surfaceHeight(baseX + x, baseZ + z);
            heights[static_cast<usize>(z * kSectionSize + x)] = surface;
            minSurface = std::min(minSurface, surface);
            maxSurface = std::max(maxSurface, surface);
        }
    }

    for (usize index = 0; index < Chunk::kSectionCount; ++index) {
        Section& section = chunk.sectionByIndex(index);

        const i32 sectionMinY = sectionIndexToWorldY(static_cast<i32>(index));
        const i32 sectionMaxY = sectionMinY + kSectionSize - 1;

        // The two uniform cases are the reason a 384-block world height is
        // affordable at all (DESIGN.md 3.5): they store one palette entry and no
        // index array, and between them they cover most of a column.
        if (sectionMinY > maxSurface) {
            section.fill(kAirBlock);
            continue;
        }
        if (sectionMaxY < minSurface - kSoilDepth) {
            section.fill(kStoneBlock);
            continue;
        }

        // Straddles the surface, so it has to be filled voxel by voxel. X innermost,
        // matching the storage order in localIndex().
        section.fill(kAirBlock);
        for (i32 localY = 0; localY < kSectionSize; ++localY) {
            const i32 worldY = sectionMinY + localY;
            for (i32 z = 0; z < kSectionSize; ++z) {
                for (i32 x = 0; x < kSectionSize; ++x) {
                    const i32 surface = heights[static_cast<usize>(z * kSectionSize + x)];
                    const BlockId block = blockFor(worldY, surface);
                    if (block != kAirBlock) {
                        section.set(x, localY, z, block);
                    }
                }
            }
        }
    }

    chunk.markAllDirty();
    chunk.setState(ChunkState::Ready);
}

} // namespace mc
