#pragma once

#include "core/Types.hpp"
#include "world/Coords.hpp"
#include "worldgen/DensityField.hpp"

#include <array>
#include <memory>

namespace mc {

/// The noise graph behind terrain generation.
///
/// FastNoise2 appears nowhere in this header. It is a PRIVATE dependency of
/// `mc_worldgen` and lives entirely inside the implementation, behind a pointer --
/// the same rule Window applies to GLFW and Device to glad. Swapping the noise
/// backend, or evaluating these fields on the GPU later, changes one file.
///
/// The channels follow Minecraft's noise router (DESIGN.md 3.12): continentalness
/// and erosion decide how high and how flat, peaks-and-valleys adds ridges, and a
/// 3D field warps the result so overhangs and caves are possible at all. They are
/// evaluated on DensityField's coarse grid, never per voxel.
class DensityGraph {
public:
    explicit DensityGraph(u32 seed);
    ~DensityGraph();

    DensityGraph(const DensityGraph&) = delete;
    DensityGraph& operator=(const DensityGraph&) = delete;

    /// The 2D climate fields for one column, on the coarse grid's horizontal
    /// footprint. Kept separate from the density because surface rules and (later)
    /// biome selection need them, and because they are 81 samples rather than 3,969.
    struct Climate {
        static constexpr usize kCount =
            static_cast<usize>(DensityField::kGridX) * static_cast<usize>(DensityField::kGridZ);

        std::array<f32, kCount> continentalness{};
        std::array<f32, kCount> erosion{};
        std::array<f32, kCount> peaksValleys{};

        static constexpr usize index(i32 gx, i32 gz) {
            return static_cast<usize>(gz) * static_cast<usize>(DensityField::kGridX)
                   + static_cast<usize>(gx);
        }

        /// Bilinearly interpolated value at a voxel column, for surface rules.
        f32 sampleErosion(i32 localX, i32 localZ) const;
        f32 sampleContinentalness(i32 localX, i32 localZ) const;
    };

    /// Evaluates every channel for one column. Thread-safe: reads only the graph,
    /// writes only the outputs.
    void fillColumn(ChunkPos pos, Climate& climate, DensityField& density) const;

    /// Which SIMD level FastNoise2's runtime dispatch actually chose, as a name.
    /// Worth logging once: it is the difference between AVX2 and SSE2 throughput.
    const char* simdLevelName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mc
