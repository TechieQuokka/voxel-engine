#pragma once

#include "core/Assert.hpp"
#include "core/Types.hpp"

namespace mc {

// World dimensions. See docs/DESIGN.md sections 3.3 and 3.4.
inline constexpr i32 kSectionSize = 32;
inline constexpr i32 kSectionSizeLog2 = 5;
inline constexpr usize kSectionVolume =
    static_cast<usize>(kSectionSize) * kSectionSize * kSectionSize;

inline constexpr i32 kWorldMinY = -64;
inline constexpr i32 kWorldMaxY = 320;
inline constexpr i32 kWorldHeight = kWorldMaxY - kWorldMinY;   // 384
inline constexpr i32 kSectionsPerColumn = kWorldHeight / kSectionSize; // 12

/// Direction of a block face.
///
/// Lives here rather than in `mesh` because a face direction is a property of
/// the world grid, not of any particular meshing strategy -- and `world` must
/// not depend on `mesh`. The order is mirrored by the tangent tables in
/// chunk.vert and must not be changed independently of them.
enum class Face : u32 {
    NegX = 0,
    PosX = 1,
    NegY = 2,
    PosY = 3,
    NegZ = 4,
    PosZ = 5,
};

inline constexpr u32 kFaceCount = 6;

/// Absolute block position in the world.
struct BlockPos {
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;

    friend constexpr bool operator==(const BlockPos&, const BlockPos&) = default;
};

/// A vertical column of sections, addressed in section-sized horizontal units.
struct ChunkPos {
    i32 x = 0;
    i32 z = 0;

    friend constexpr bool operator==(const ChunkPos&, const ChunkPos&) = default;
};

/// A single 32^3 section. `y` is a section index, not a block coordinate.
struct SectionPos {
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;

    friend constexpr bool operator==(const SectionPos&, const SectionPos&) = default;
};

/// Arithmetic shift, so negative coordinates floor toward negative infinity
/// rather than truncating toward zero. Plain division would make the sections
/// straddling x = 0 twice as wide as every other one.
constexpr i32 blockToSectionCoord(i32 block) {
    return block >> kSectionSizeLog2;
}

/// Position within a section, always in [0, 32).
constexpr i32 blockToLocalCoord(i32 block) {
    return block & (kSectionSize - 1);
}

constexpr ChunkPos toChunkPos(BlockPos pos) {
    return {blockToSectionCoord(pos.x), blockToSectionCoord(pos.z)};
}

constexpr SectionPos toSectionPos(BlockPos pos) {
    return {blockToSectionCoord(pos.x), blockToSectionCoord(pos.y), blockToSectionCoord(pos.z)};
}

/// Section index within a column, 0 at kWorldMinY.
constexpr i32 sectionIndexInColumn(i32 sectionY) {
    return sectionY - blockToSectionCoord(kWorldMinY);
}

constexpr bool isValidWorldY(i32 y) {
    return y >= kWorldMinY && y < kWorldMaxY;
}

/// Flattens a local coordinate to a storage index.
///
/// X varies fastest so that a run along X is contiguous in memory. Both the
/// mesher and the generator walk X innermost, so this is the ordering that
/// keeps them on the same cache lines.
constexpr usize localIndex(i32 x, i32 y, i32 z) {
    MC_ASSERT(x >= 0 && x < kSectionSize);
    MC_ASSERT(y >= 0 && y < kSectionSize);
    MC_ASSERT(z >= 0 && z < kSectionSize);
    return (static_cast<usize>(y) * static_cast<usize>(kSectionSize) + static_cast<usize>(z))
               * static_cast<usize>(kSectionSize)
           + static_cast<usize>(x);
}

} // namespace mc
