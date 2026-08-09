#include "world/Coords.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace mc;

TEST_CASE("world dimensions are consistent") {
    CHECK(kSectionSize == 32);
    CHECK((1 << kSectionSizeLog2) == kSectionSize);
    CHECK(kSectionVolume == 32768);
    CHECK(kWorldHeight == 384);
    CHECK(kSectionsPerColumn == 12);
    CHECK(kWorldHeight % kSectionSize == 0);
}

TEST_CASE("blockToSectionCoord floors toward negative infinity") {
    // Truncating division would make the section straddling 0 twice as wide as
    // every other one, which silently misplaces terrain west and north of the
    // origin.
    CHECK(blockToSectionCoord(0) == 0);
    CHECK(blockToSectionCoord(31) == 0);
    CHECK(blockToSectionCoord(32) == 1);
    CHECK(blockToSectionCoord(-1) == -1);
    CHECK(blockToSectionCoord(-32) == -1);
    CHECK(blockToSectionCoord(-33) == -2);
}

TEST_CASE("blockToLocalCoord is always in range") {
    for (i32 block = -100; block <= 100; ++block) {
        const i32 local = blockToLocalCoord(block);
        REQUIRE(local >= 0);
        REQUIRE(local < kSectionSize);
        // The pair (section, local) must reconstruct the original coordinate.
        REQUIRE(blockToSectionCoord(block) * kSectionSize + local == block);
    }
}

TEST_CASE("toSectionPos maps negative coordinates correctly") {
    const SectionPos pos = toSectionPos(BlockPos{-1, -1, -1});
    CHECK(pos == SectionPos{-1, -1, -1});

    const SectionPos origin = toSectionPos(BlockPos{0, 0, 0});
    CHECK(origin == SectionPos{0, 0, 0});
}

TEST_CASE("sectionIndexInColumn is zero at the world floor") {
    CHECK(sectionIndexInColumn(blockToSectionCoord(kWorldMinY)) == 0);
    CHECK(sectionIndexInColumn(blockToSectionCoord(kWorldMaxY - 1)) == kSectionsPerColumn - 1);
}

TEST_CASE("isValidWorldY covers exactly the world range") {
    CHECK_FALSE(isValidWorldY(kWorldMinY - 1));
    CHECK(isValidWorldY(kWorldMinY));
    CHECK(isValidWorldY(kWorldMaxY - 1));
    CHECK_FALSE(isValidWorldY(kWorldMaxY));
}

TEST_CASE("localIndex is a bijection over the section volume") {
    std::vector<bool> seen(kSectionVolume, false);

    for (i32 y = 0; y < kSectionSize; ++y) {
        for (i32 z = 0; z < kSectionSize; ++z) {
            for (i32 x = 0; x < kSectionSize; ++x) {
                const usize index = localIndex(x, y, z);
                REQUIRE(index < kSectionVolume);
                REQUIRE_FALSE(seen[index]);
                seen[index] = true;
            }
        }
    }
}

TEST_CASE("X is contiguous in storage order") {
    // The mesher and the generator both walk X innermost; this is what keeps
    // them on the same cache lines.
    CHECK(localIndex(1, 0, 0) - localIndex(0, 0, 0) == 1);
    CHECK(localIndex(0, 0, 1) - localIndex(0, 0, 0) == kSectionSize);
    CHECK(localIndex(0, 1, 0) - localIndex(0, 0, 0) == kSectionSize * kSectionSize);
}
