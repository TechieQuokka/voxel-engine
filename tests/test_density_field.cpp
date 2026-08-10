#include "worldgen/DensityField.hpp"

#include <doctest/doctest.h>

using namespace mc;

TEST_CASE("the grid covers the column with one plane to spare on each axis") {
    // The +1 is the whole reason interpolating the last voxel works. If the grid were
    // 8 wide, sampling local x = 31 would read past the end.
    CHECK(DensityField::kGridX == kSectionSize / DensityField::kCellWidth + 1);
    CHECK(DensityField::kGridX == 9);
    CHECK(DensityField::kGridY == kWorldHeight / DensityField::kCellHeight + 1);
    CHECK(DensityField::kGridY == 49);

    CHECK(DensityField::gridYToWorldY(0) == kWorldMinY);
    CHECK(DensityField::gridYToWorldY(DensityField::kGridY - 1) == kWorldMaxY);
}

TEST_CASE("the grid is two orders of magnitude smaller than the column") {
    // The measurement that justifies the whole class: 3,969 samples instead of
    // 393,216 evaluations, a factor of 99.
    constexpr usize voxels = static_cast<usize>(kSectionSize) * kSectionSize
                           * static_cast<usize>(kWorldHeight);
    CHECK(voxels == 393216);
    CHECK(DensityField::kSampleCount == 3969);
    CHECK(voxels / DensityField::kSampleCount == 99);
}

TEST_CASE("index matches FastNoise2's x-fastest-then-y-then-z layout") {
    // Getting this wrong scrambles the field into horizontal sheets, which looks like
    // a terrain bug rather than an indexing one.
    CHECK(DensityField::index(0, 0, 0) == 0);
    CHECK(DensityField::index(1, 0, 0) == 1);
    CHECK(DensityField::index(0, 1, 0) == static_cast<usize>(DensityField::kGridX));
    CHECK(DensityField::index(0, 0, 1)
          == static_cast<usize>(DensityField::kGridX) * DensityField::kGridY);
}

TEST_CASE("a constant field interpolates to that constant everywhere") {
    DensityField field;
    for (f32& sample : field.samples()) {
        sample = 2.5f;
    }

    for (i32 y = kWorldMinY; y < kWorldMaxY; y += 7) {
        for (i32 x = 0; x < kSectionSize; x += 5) {
            for (i32 z = 0; z < kSectionSize; z += 5) {
                CAPTURE(x);
                CAPTURE(y);
                CAPTURE(z);
                REQUIRE(field.sample(x, y, z) == doctest::Approx(2.5f));
            }
        }
    }
}

TEST_CASE("grid points reproduce their sample exactly") {
    DensityField field;
    // A value that depends on all three axes, so a transposed index shows up.
    for (i32 gz = 0; gz < DensityField::kGridZ; ++gz) {
        for (i32 gy = 0; gy < DensityField::kGridY; ++gy) {
            for (i32 gx = 0; gx < DensityField::kGridX; ++gx) {
                field.at(gx, gy, gz) =
                    static_cast<f32>(gx) + 100.0f * static_cast<f32>(gy)
                    + 10000.0f * static_cast<f32>(gz);
            }
        }
    }

    for (i32 gz = 0; gz < DensityField::kGridZ - 1; ++gz) {
        for (i32 gy = 0; gy < DensityField::kGridY - 1; ++gy) {
            for (i32 gx = 0; gx < DensityField::kGridX - 1; ++gx) {
                const i32 x = gx * DensityField::kCellWidth;
                const i32 z = gz * DensityField::kCellWidth;
                const i32 y = DensityField::gridYToWorldY(gy);
                CAPTURE(gx);
                CAPTURE(gy);
                CAPTURE(gz);
                REQUIRE(field.sample(x, y, z) == doctest::Approx(field.at(gx, gy, gz)));
            }
        }
    }
}

TEST_CASE("interpolation along one axis is linear") {
    DensityField field;
    for (f32& sample : field.samples()) {
        sample = 0.0f;
    }
    // 0 at gx = 0, 8 at gx = 1, so a step of 1 per block across the first cell.
    for (i32 gz = 0; gz < DensityField::kGridZ; ++gz) {
        for (i32 gy = 0; gy < DensityField::kGridY; ++gy) {
            field.at(1, gy, gz) = 8.0f;
        }
    }

    const i32 y = 0;
    CHECK(field.sample(0, y, 0) == doctest::Approx(0.0f));
    CHECK(field.sample(1, y, 0) == doctest::Approx(2.0f));
    CHECK(field.sample(2, y, 0) == doctest::Approx(4.0f));
    CHECK(field.sample(3, y, 0) == doctest::Approx(6.0f));
    // x = 4 is the next grid point, which is back to 0 because gx = 2 was left there.
    CHECK(field.sample(4, y, 0) == doctest::Approx(8.0f));
}

TEST_CASE("interpolation along Y spans eight blocks, not four") {
    // The vertical cell is twice the horizontal one -- Minecraft's size_vertical = 2.
    // Mixing the two up gives terrain that is subtly wrong everywhere.
    DensityField field;
    for (f32& sample : field.samples()) {
        sample = 0.0f;
    }
    for (i32 gz = 0; gz < DensityField::kGridZ; ++gz) {
        for (i32 gx = 0; gx < DensityField::kGridX; ++gx) {
            field.at(gx, 1, gz) = 8.0f;
        }
    }

    const i32 base = DensityField::gridYToWorldY(0);
    CHECK(field.sample(0, base + 0, 0) == doctest::Approx(0.0f));
    CHECK(field.sample(0, base + 4, 0) == doctest::Approx(4.0f)); // Halfway up the cell.
    CHECK(field.sample(0, base + 7, 0) == doctest::Approx(7.0f));
    CHECK(field.sample(0, base + 8, 0) == doctest::Approx(8.0f)); // The next grid plane.
}

TEST_CASE("the interpolated value never leaves the range of its eight corners") {
    // Trilinear interpolation is a convex combination, so this must hold. It is the
    // property that lets the generator threshold the interpolated value directly
    // rather than worrying about overshoot.
    DensityField field;
    u32 state = 12345u;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<f32>(state >> 8) / static_cast<f32>(1u << 24) * 2.0f - 1.0f;
    };
    for (f32& sample : field.samples()) {
        sample = next();
    }

    for (i32 y = kWorldMinY; y < kWorldMaxY; y += 13) {
        for (i32 x = 0; x < kSectionSize; x += 3) {
            for (i32 z = 0; z < kSectionSize; z += 3) {
                const i32 gx = x / DensityField::kCellWidth;
                const i32 gz = z / DensityField::kCellWidth;
                const i32 gy = (y - kWorldMinY) / DensityField::kCellHeight;

                f32 lo = field.at(gx, gy, gz);
                f32 hi = lo;
                for (i32 dz = 0; dz <= 1; ++dz) {
                    for (i32 dy = 0; dy <= 1; ++dy) {
                        for (i32 dx = 0; dx <= 1; ++dx) {
                            const f32 corner = field.at(gx + dx, gy + dy, gz + dz);
                            lo = corner < lo ? corner : lo;
                            hi = corner > hi ? corner : hi;
                        }
                    }
                }

                const f32 value = field.sample(x, y, z);
                CAPTURE(x);
                CAPTURE(y);
                CAPTURE(z);
                REQUIRE(value >= lo - 1e-5f);
                REQUIRE(value <= hi + 1e-5f);
            }
        }
    }
}
