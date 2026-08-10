#pragma once

#include "core/Assert.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <array>
#include <span>

namespace mc {

/// Density samples for one chunk column on a coarse grid, plus the interpolation
/// that turns them back into a value per voxel.
///
/// **This is the single most important thing the Minecraft research turned up.**
/// Minecraft does not evaluate its density function per block. `noise_settings`
/// exposes `size_horizontal` and `size_vertical`, which set an interpolation cell of
/// `4 * size` blocks on each axis; the Overworld uses 1 and 2, so one sample covers
/// 4x8x4 = 128 blocks and everything between is trilinearly interpolated.
///
/// DESIGN.md 4.1 had assumed a density evaluation per voxel and sized the noise
/// budget accordingly. For a 32x32x384 column that is 393,216 evaluations; on the
/// grid it is 9 x 49 x 9 = 3,969, a factor of 99. The noise library was never the
/// thing that needed to be fast -- not calling it was.
///
/// The interpolation is not only a saving, it is part of the look: smoothing the
/// density field in Y is what gives Minecraft's terrain its layered, terraced feel
/// rather than the blobby result of sampling 3D noise per voxel.
///
/// Layout matches FastNoise2's `GenUniformGrid3D` output order -- x fastest, then y,
/// then z -- so the grid can be handed to it directly with no shuffle.
class DensityField {
public:
    /// Minecraft's size_horizontal = 1 and size_vertical = 2.
    static constexpr i32 kCellWidth = 4;
    static constexpr i32 kCellHeight = 8;

    static_assert(kSectionSize % kCellWidth == 0, "a section must be a whole number of cells");
    static_assert(kWorldHeight % kCellHeight == 0, "the world must be a whole number of cells");

    /// One extra plane per axis: interpolating the last voxel needs the sample past
    /// it. This is why the grid is 9 wide for a 32-block column and not 8.
    static constexpr i32 kGridX = kSectionSize / kCellWidth + 1;    // 9
    static constexpr i32 kGridZ = kGridX;                           // 9
    static constexpr i32 kGridY = kWorldHeight / kCellHeight + 1;   // 49

    static constexpr usize kSampleCount =
        static_cast<usize>(kGridX) * static_cast<usize>(kGridY) * static_cast<usize>(kGridZ);

    static constexpr usize index(i32 gx, i32 gy, i32 gz) {
        MC_ASSERT(gx >= 0 && gx < kGridX);
        MC_ASSERT(gy >= 0 && gy < kGridY);
        MC_ASSERT(gz >= 0 && gz < kGridZ);
        return (static_cast<usize>(gz) * static_cast<usize>(kGridY) + static_cast<usize>(gy))
                   * static_cast<usize>(kGridX)
               + static_cast<usize>(gx);
    }

    f32& at(i32 gx, i32 gy, i32 gz) { return m_samples[index(gx, gy, gz)]; }
    f32 at(i32 gx, i32 gy, i32 gz) const { return m_samples[index(gx, gy, gz)]; }

    /// Writable view for a generator to fill in one call.
    std::span<f32> samples() { return m_samples; }
    std::span<const f32> samples() const { return m_samples; }

    /// World Y of a grid plane, and the inverse.
    static constexpr i32 gridYToWorldY(i32 gy) { return kWorldMinY + gy * kCellHeight; }

    /// Interpolated density at a voxel. `localX` and `localZ` are in [0, 32),
    /// `worldY` in [kWorldMinY, kWorldMaxY).
    f32 sample(i32 localX, i32 worldY, i32 localZ) const;

private:
    std::array<f32, kSampleCount> m_samples{};
};

} // namespace mc
