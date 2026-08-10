#include "worldgen/DensityField.hpp"

namespace mc {
namespace {

f32 lerp(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

} // namespace

f32 DensityField::sample(i32 localX, i32 worldY, i32 localZ) const {
    MC_ASSERT(localX >= 0 && localX < kSectionSize);
    MC_ASSERT(localZ >= 0 && localZ < kSectionSize);
    MC_ASSERT(worldY >= kWorldMinY && worldY < kWorldMaxY);

    // Cell index and the fraction within it. The divisions are by compile-time
    // powers of two, so this is a shift and a mask.
    const i32 gx = localX / kCellWidth;
    const i32 gz = localZ / kCellWidth;
    const i32 gy = (worldY - kWorldMinY) / kCellHeight;

    const f32 tx = static_cast<f32>(localX % kCellWidth) / static_cast<f32>(kCellWidth);
    const f32 tz = static_cast<f32>(localZ % kCellWidth) / static_cast<f32>(kCellWidth);
    const f32 ty =
        static_cast<f32>((worldY - kWorldMinY) % kCellHeight) / static_cast<f32>(kCellHeight);

    // Trilinear, written out rather than looped: eight loads and seven lerps, and
    // this is the innermost operation of terrain generation.
    const f32 c000 = at(gx, gy, gz);
    const f32 c100 = at(gx + 1, gy, gz);
    const f32 c010 = at(gx, gy + 1, gz);
    const f32 c110 = at(gx + 1, gy + 1, gz);
    const f32 c001 = at(gx, gy, gz + 1);
    const f32 c101 = at(gx + 1, gy, gz + 1);
    const f32 c011 = at(gx, gy + 1, gz + 1);
    const f32 c111 = at(gx + 1, gy + 1, gz + 1);

    const f32 x00 = lerp(c000, c100, tx);
    const f32 x10 = lerp(c010, c110, tx);
    const f32 x01 = lerp(c001, c101, tx);
    const f32 x11 = lerp(c011, c111, tx);

    const f32 y0 = lerp(x00, x10, ty);
    const f32 y1 = lerp(x01, x11, ty);

    return lerp(y0, y1, tz);
}

} // namespace mc
