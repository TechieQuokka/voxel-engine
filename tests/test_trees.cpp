#include "world/BlockTable.hpp"
#include "world/Chunk.hpp"
#include "worldgen/FeatureTable.hpp"
#include "worldgen/Generator.hpp"

#include <doctest/doctest.h>

#include <memory>

using namespace mc;

namespace {

/// Counts blocks of one type in a generated column, and where they sit.
struct TreeCensus {
    usize logs = 0;
    usize leaves = 0;
    /// Horizontal reach of the furthest leaf from the column's edges, as a local
    /// coordinate. Used to prove nothing touches the wall.
    i32 minLocalX = kSectionSize;
    i32 maxLocalX = -1;
    i32 minLocalZ = kSectionSize;
    i32 maxLocalZ = -1;
};

TreeCensus census(const Chunk& column) {
    TreeCensus out;
    for (i32 y = kWorldMinY; y < kWorldMaxY; ++y) {
        const Section* section = column.sectionAt(blockToSectionCoord(y));
        if (section == nullptr || section->isEmpty()) {
            continue;
        }
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                const BlockId block = section->get(x, blockToLocalCoord(y), z);
                if (block != kOakLogBlock && block != kOakLeavesBlock) {
                    continue;
                }
                if (block == kOakLogBlock) {
                    ++out.logs;
                } else {
                    ++out.leaves;
                }
                out.minLocalX = std::min(out.minLocalX, x);
                out.maxLocalX = std::max(out.maxLocalX, x);
                out.minLocalZ = std::min(out.minLocalZ, z);
                out.maxLocalZ = std::max(out.maxLocalZ, z);
            }
        }
    }
    return out;
}

std::unique_ptr<Chunk> generate(const Generator& generator, ChunkPos pos) {
    auto column = std::make_unique<Chunk>(pos);
    generator.generateColumn(*column);
    return column;
}

} // namespace

TEST_CASE("trees generate, and grow on grass") {
    const Generator generator(1234u);

    usize columnsWithTrees = 0;
    usize totalLogs = 0;

    for (i32 z = 0; z < 4; ++z) {
        for (i32 x = 0; x < 4; ++x) {
            const auto column = generate(generator, ChunkPos{x, z});
            const TreeCensus counts = census(*column);
            if (counts.logs > 0) {
                ++columnsWithTrees;
            }
            totalLogs += counts.logs;
        }
    }

    // Sixteen columns at five attempts each. Most attempts land on grass, so this
    // should be comfortably non-zero without being a solid forest.
    CHECK(columnsWithTrees > 0);
    CHECK(totalLogs > 16);
}

TEST_CASE("a tree never crosses a column border") {
    // This is the whole compromise TreeSpec documents: trees are inset so they fit
    // inside their own column, because a neighbour cannot know how high this
    // column's ground is. If a tree ever touched the wall, the replay problem would
    // be back and neighbouring columns would disagree about the block there.
    const Generator generator(99u);

    for (i32 z = -2; z <= 2; ++z) {
        for (i32 x = -2; x <= 2; ++x) {
            const auto column = generate(generator, ChunkPos{x, z});
            const TreeCensus counts = census(*column);
            if (counts.logs == 0 && counts.leaves == 0) {
                continue;
            }

            CAPTURE(x);
            CAPTURE(z);
            CHECK(counts.minLocalX >= 0);
            CHECK(counts.minLocalZ >= 0);
            CHECK(counts.maxLocalX <= kSectionSize - 1);
            CHECK(counts.maxLocalZ <= kSectionSize - 1);
        }
    }
}

TEST_CASE("generation is deterministic, trees included") {
    // The 3x3 blob replay depends on this for the blobs; trees do not replay, but a
    // tree that moved between runs would still make the world unreproducible.
    const Generator generator(7u);

    const auto first = generate(generator, ChunkPos{3, -5});
    const auto second = generate(generator, ChunkPos{3, -5});

    const TreeCensus a = census(*first);
    const TreeCensus b = census(*second);

    CHECK(a.logs == b.logs);
    CHECK(a.leaves == b.leaves);
    CHECK(a.minLocalX == b.minLocalX);
    CHECK(a.maxLocalZ == b.maxLocalZ);
}

TEST_CASE("leaves sit above the ground they grew from") {
    const Generator generator(4242u);
    const auto column = generate(generator, ChunkPos{0, 0});

    // Every log should have solid ground somewhere below it and air or leaves
    // directly above the trunk top -- a tree buried in stone would mean the surface
    // test failed.
    for (i32 y = kWorldMinY + 1; y < kWorldMaxY; ++y) {
        const Section* section = column->sectionAt(blockToSectionCoord(y));
        if (section == nullptr || section->isEmpty()) {
            continue;
        }
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                if (section->get(x, blockToLocalCoord(y), z) != kOakLogBlock) {
                    continue;
                }
                // Walk down to the first non-log block; it must be ground, not air.
                i32 below = y - 1;
                while (below > kWorldMinY) {
                    const Section* under = column->sectionAt(blockToSectionCoord(below));
                    if (under == nullptr) {
                        break;
                    }
                    const BlockId block = under->get(x, blockToLocalCoord(below), z);
                    if (block != kOakLogBlock) {
                        CAPTURE(x);
                        CAPTURE(y);
                        CAPTURE(z);
                        CHECK(block != kAirBlock);
                        break;
                    }
                    --below;
                }
            }
        }
    }
}
