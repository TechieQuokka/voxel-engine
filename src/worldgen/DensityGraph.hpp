#pragma once

#include "core/Types.hpp"
#include "world/Coords.hpp"
#include "worldgen/DensityField.hpp"

#include <array>
#include <memory>
#include <span>

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
    ///
    /// Cheese caves are folded into `density` here, because they are large enough to
    /// survive the interpolation grid. Thin caves are not — see carveThinCaves.
    void fillColumn(ChunkPos pos, Climate& climate, DensityField& density) const;

    /// Vertical extent within which thin caves are carved. Outside it the sky and the
    /// deep floor need no per-block work at all.
    static constexpr i32 kCaveMinY = kWorldMinY + 5;
    static constexpr i32 kCaveMaxY = 120;

    /// At or below this the density field is forced solid, so the world has a floor.
    ///
    /// Public because `Generator` needs it too: this is the height the bedrock layer
    /// fills, and a second copy of the number in the generator would be a constant
    /// maintained in two files that must agree.
    static constexpr i32 kBedrockTop = kWorldMinY + 4;

    /// Per-block spaghetti and noodle carving for one section, as a solidity mask.
    ///
    /// **This one cannot use the interpolation grid, and that is the whole point.** A
    /// noodle tunnel is one to five blocks across; the grid's vertical cell is eight,
    /// so interpolation would erase it. Minecraft has the same problem and answers it
    /// the same way — some entries in its noise router are explicitly not interpolated.
    ///
    /// So this is the one place the engine does pay for noise per voxel, and it is
    /// bounded three ways: only sections that already hold both rock and air, only
    /// within [kCaveMinY, kCaveMaxY], and one SIMD grid call per section rather than a
    /// call per block.
    ///
    /// `out` is 32^3 entries: 1 where the block survives, 0 where the cave takes it.
    /// Everything already air stays air.
    void carveThinCaves(SectionPos pos, std::span<u8> solid) const;

    /// True when a section's Y range can contain a thin cave at all.
    static bool thinCavesReach(i32 sectionY) {
        const i32 minY = sectionY * kSectionSize;
        return minY + kSectionSize > kCaveMinY && minY <= kCaveMaxY;
    }

    /// Which SIMD level FastNoise2's runtime dispatch actually chose, as a name.
    /// Worth logging once: it is the difference between AVX2 and SSE2 throughput.
    const char* simdLevelName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mc
