#include "worldgen/DensityGraph.hpp"

#include "core/Assert.hpp"
#include "core/Profile.hpp"

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <vector>

namespace mc {
namespace {

/// Terrain shaping constants. These are the knobs that decide what the world looks
/// like, so they are gathered here rather than scattered through the code.
namespace shape {

/// Height the terrain varies around, and how far it may swing.
///
/// The amplitudes were set by measurement, not by eye: a transect of 8,000 blocks is
/// expected to span roughly 100 blocks of height with a local relief of 40 or more
/// over any 128 blocks, which is about what Minecraft's overworld does between plains
/// and mountains. The first attempt at these numbers gave a range of 38 and a relief
/// of 14 -- terrain that reads as a contour map rather than a landscape.
constexpr f32 kBaseHeight = 64.0f;
constexpr f32 kContinentAmplitude = 52.0f;
constexpr f32 kRidgeAmplitude = 58.0f;

/// How sharply density crosses zero at the target height.
///
/// This is Minecraft's "squeeze": a large value spreads the transition over more
/// blocks, which lets the 3D field push solid ground above the surface and carve air
/// below it -- overhangs and cliffs. A small value gives a heightmap with no
/// overhangs at all, whatever the 3D noise does.
constexpr f32 kSqueeze = 30.0f;

/// Weight of the 3D field against the height term.
constexpr f32 kWarpStrength = 0.9f;

/// Below this the world is solid regardless of noise, so the world has a floor.
constexpr i32 kBedrockTop = kWorldMinY + 4;

/// Above this, density is forced negative so terrain cannot reach the build limit.
constexpr f32 kFadeStartY = 200.0f;

} // namespace shape

/// Cave tuning. The three kinds are the 1.18 model: wide cheese pockets, long thin
/// spaghetti connecting them, and thinner noodle branches off those.
namespace cave {

/// Cheese caves ride the density grid, so they must be larger than a grid cell (4x8x4)
/// to survive interpolation at all. This is the amount of density they subtract where
/// the cheese field is strongest.
/// Tuned against a measured underground air fraction and an ASCII cross-section, not by
/// eye on the surface -- from above, a world with no caves at all looks identical.
constexpr f32 kCheeseStrength = 5.5f;
/// Only where the field exceeds this, so caverns are pockets rather than a sponge.
constexpr f32 kCheeseThreshold = 0.28f;

/// Spaghetti and noodle are carved per block. A tunnel exists where the ridged field is
/// near its crest, so the width is set by how near.
///
/// Tuned in three steps, each measured. 0.055 and 0.022 produced single-block slits
/// rather than tunnels -- visible in a cross-section only if you knew to look. Widening
/// to 0.16 and 0.07 overshot to a 12.9% underground air fraction, well above
/// Minecraft's. These land at 6.8%, inside the 3-8% range Minecraft occupies.
///
/// Cost scales with air fraction, and steeply: 1.7% air was 2.0 M quads at distance 16,
/// 6.8% is 4.1 M, 10.8% is 4.7 M. All of it is cave wall that nothing outside can see,
/// which is what makes occlusion culling (Phase 8) the answer rather than thinner caves.
constexpr f32 kSpaghettiWidth = 0.115f;
constexpr f32 kNoodleWidth = 0.045f;

/// Caves fade out near the surface so the world is not visibly perforated from above,
/// and near the floor so there is always rock underneath.
constexpr i32 kSurfaceMargin = 8;
constexpr i32 kFloorMargin = 6;

} // namespace cave

/// Frequencies, in cycles per block. The climate fields are deliberately far lower
/// frequency than the 3D warp: continents should span hundreds of blocks while
/// overhangs span tens.
namespace freq {
constexpr f32 kContinentalness = 0.00035f;
constexpr f32 kErosion = 0.00055f;
constexpr f32 kPeaksValleys = 0.0045f;
constexpr f32 kDensity3D = 0.0075f;
constexpr f32 kCheese = 0.0125f;
constexpr f32 kSpaghetti = 0.021f;
constexpr f32 kNoodle = 0.047f;
} // namespace freq

FastNoise::SmartNode<FastNoise::FractalFBm> makeFbm(int octaves, f32 gain, f32 lacunarity) {
    auto source = FastNoise::New<FastNoise::Perlin>();
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(source);
    fractal->SetOctaveCount(octaves);
    fractal->SetGain(gain);
    fractal->SetLacunarity(lacunarity);
    return fractal;
}

/// Ridged fractal, for peaks and valleys.
///
/// FBm cannot make a mountain ridge -- it is symmetric about zero, so it produces
/// rolling hills whatever the amplitude. Ridged noise folds the absolute value, which
/// puts a sharp crest where the underlying noise crosses zero. Minecraft reaches the
/// same shape by folding weirdness into its PV term; this is the direct equivalent.
FastNoise::SmartNode<FastNoise::FractalRidged> makeRidged(int octaves, f32 gain,
                                                          f32 lacunarity) {
    auto source = FastNoise::New<FastNoise::Perlin>();
    auto fractal = FastNoise::New<FastNoise::FractalRidged>();
    fractal->SetSource(source);
    fractal->SetOctaveCount(octaves);
    fractal->SetGain(gain);
    fractal->SetLacunarity(lacunarity);
    return fractal;
}

const char* nameOf(FastSIMD::eLevel level) {
    switch (level) {
    case FastSIMD::Level_Scalar: return "scalar";
    case FastSIMD::Level_SSE:    return "SSE";
    case FastSIMD::Level_SSE2:   return "SSE2";
    case FastSIMD::Level_SSE3:   return "SSE3";
    case FastSIMD::Level_SSSE3:  return "SSSE3";
    case FastSIMD::Level_SSE41:  return "SSE4.1";
    case FastSIMD::Level_SSE42:  return "SSE4.2";
    case FastSIMD::Level_AVX:    return "AVX";
    case FastSIMD::Level_AVX2:   return "AVX2";
    case FastSIMD::Level_AVX512: return "AVX512";
    default:                     return "unknown";
    }
}

/// Maps a climate value in [-1, 1] through a piecewise-linear spline.
///
/// Minecraft uses cubic splines here, nested three deep. Piecewise-linear over a
/// handful of points is enough to get the same *shape* -- flat oceans, a steep
/// coastal rise, a plateau inland -- and it is far easier to reason about when the
/// terrain comes out wrong. Upgrading to cubic changes only this function.
f32 spline(f32 t, std::span<const f32> xs, std::span<const f32> ys) {
    MC_ASSERT(xs.size() == ys.size() && xs.size() >= 2);

    if (t <= xs.front()) {
        return ys.front();
    }
    if (t >= xs.back()) {
        return ys.back();
    }
    for (usize i = 1; i < xs.size(); ++i) {
        if (t <= xs[i]) {
            const f32 span = xs[i] - xs[i - 1];
            const f32 local = (t - xs[i - 1]) / span;
            return ys[i - 1] + (ys[i] - ys[i - 1]) * local;
        }
    }
    return ys.back();
}

/// Continentalness -> height offset. Oceans, then a coast, then inland.
f32 continentSpline(f32 continentalness) {
    static constexpr std::array<f32, 5> kXs{-1.0f, -0.45f, -0.15f, 0.2f, 1.0f};
    static constexpr std::array<f32, 5> kYs{-0.75f, -0.35f, 0.05f, 0.45f, 1.0f};
    return spline(continentalness, kXs, kYs);
}

/// Erosion -> how much of the ridge amplitude survives. High erosion is flat.
f32 erosionSpline(f32 erosion) {
    static constexpr std::array<f32, 5> kXs{-1.0f, -0.375f, 0.05f, 0.45f, 1.0f};
    static constexpr std::array<f32, 5> kYs{1.0f, 0.7f, 0.35f, 0.12f, 0.05f};
    return spline(erosion, kXs, kYs);
}

} // namespace

struct DensityGraph::Impl {
    u32 seed;

    FastNoise::SmartNode<FastNoise::FractalFBm> continentalness;
    FastNoise::SmartNode<FastNoise::FractalFBm> erosion;
    FastNoise::SmartNode<FastNoise::FractalRidged> peaksValleys;
    FastNoise::SmartNode<FastNoise::DomainAxisScale> density3D;

    /// Wide caverns, on the density grid.
    FastNoise::SmartNode<FastNoise::DomainAxisScale> cheese;
    /// Thin tunnels, per block. Ridged so that a tunnel follows the crest of the field
    /// rather than filling a blob -- the same reason peaks-and-valleys is ridged.
    FastNoise::SmartNode<FastNoise::FractalRidged> spaghetti;
    FastNoise::SmartNode<FastNoise::FractalRidged> noodle;

    /// Scratch for the 3D grid, so fillColumn does not allocate. One per graph, and
    /// the graph is shared across threads -- so this cannot live here. It is
    /// thread_local in fillColumn instead; see there.
    explicit Impl(u32 s) : seed(s) {
        continentalness = makeFbm(3, 0.5f, 2.0f);
        erosion = makeFbm(3, 0.5f, 2.0f);
        peaksValleys = makeRidged(4, 0.5f, 2.1f);

        // The 3D field is squashed vertically, so features are wider than they are
        // tall. Without this, 3D noise produces round blobs; with it, ledges.
        auto base = makeFbm(4, 0.5f, 2.0f);
        density3D = FastNoise::New<FastNoise::DomainAxisScale>();
        density3D->SetSource(base);
        density3D->SetScale<FastNoise::Dim::X>(1.0f);
        density3D->SetScale<FastNoise::Dim::Y>(0.55f);
        density3D->SetScale<FastNoise::Dim::Z>(1.0f);

        // Cheese caverns are flattened harder than the terrain field: a cavern that is
        // wider than it is tall reads as a cave, one that is spherical reads as a bubble.
        auto cheeseBase = makeFbm(3, 0.5f, 2.0f);
        cheese = FastNoise::New<FastNoise::DomainAxisScale>();
        cheese->SetSource(cheeseBase);
        cheese->SetScale<FastNoise::Dim::X>(1.0f);
        cheese->SetScale<FastNoise::Dim::Y>(2.1f);
        cheese->SetScale<FastNoise::Dim::Z>(1.0f);

        spaghetti = makeRidged(2, 0.5f, 2.0f);
        noodle = makeRidged(2, 0.5f, 2.0f);
    }
};

DensityGraph::DensityGraph(u32 seed) : m_impl(std::make_unique<Impl>(seed)) {}

DensityGraph::~DensityGraph() = default;

const char* DensityGraph::simdLevelName() const {
    return nameOf(m_impl->continentalness->GetSIMDLevel());
}

f32 DensityGraph::Climate::sampleContinentalness(i32 localX, i32 localZ) const {
    const i32 gx = localX / DensityField::kCellWidth;
    const i32 gz = localZ / DensityField::kCellWidth;
    const f32 tx = static_cast<f32>(localX % DensityField::kCellWidth)
                 / static_cast<f32>(DensityField::kCellWidth);
    const f32 tz = static_cast<f32>(localZ % DensityField::kCellWidth)
                 / static_cast<f32>(DensityField::kCellWidth);

    const f32 a = continentalness[index(gx, gz)];
    const f32 b = continentalness[index(gx + 1, gz)];
    const f32 c = continentalness[index(gx, gz + 1)];
    const f32 d = continentalness[index(gx + 1, gz + 1)];

    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

f32 DensityGraph::Climate::sampleErosion(i32 localX, i32 localZ) const {
    const i32 gx = localX / DensityField::kCellWidth;
    const i32 gz = localZ / DensityField::kCellWidth;
    const f32 tx = static_cast<f32>(localX % DensityField::kCellWidth)
                 / static_cast<f32>(DensityField::kCellWidth);
    const f32 tz = static_cast<f32>(localZ % DensityField::kCellWidth)
                 / static_cast<f32>(DensityField::kCellWidth);

    const f32 a = erosion[index(gx, gz)];
    const f32 b = erosion[index(gx + 1, gz)];
    const f32 c = erosion[index(gx, gz + 1)];
    const f32 d = erosion[index(gx + 1, gz + 1)];

    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

void DensityGraph::fillColumn(ChunkPos pos, Climate& climate, DensityField& density) const {
    MC_PROFILE_SCOPE_N("DensityGraph::fillColumn");

    // Grid origin in blocks. The grid steps by kCellWidth, so in grid units the
    // origin is the column's block origin divided by the cell width -- which is what
    // GenUniformGrid* wants, given a frequency scaled by the same factor.
    const i32 originX = pos.x * kSectionSize / DensityField::kCellWidth;
    const i32 originZ = pos.z * kSectionSize / DensityField::kCellWidth;

    // The grid steps by one cell, so the frequency is scaled by the cell width to
    // stay expressed in cycles per *block*. Only the width appears: the Y cell is
    // twice as tall, and DomainAxisScale on Y already accounts for that anisotropy.
    constexpr f32 kCellW = static_cast<f32>(DensityField::kCellWidth);

    // 2D climate: 9 x 9 = 81 samples per column.
    {
        MC_PROFILE_SCOPE_N("climate");
        m_impl->continentalness->GenUniformGrid2D(
            climate.continentalness.data(), originX, originZ,
            DensityField::kGridX, DensityField::kGridZ,
            freq::kContinentalness * kCellW, static_cast<int>(m_impl->seed));

        m_impl->erosion->GenUniformGrid2D(
            climate.erosion.data(), originX, originZ,
            DensityField::kGridX, DensityField::kGridZ,
            freq::kErosion * kCellW, static_cast<int>(m_impl->seed) + 1301);

        m_impl->peaksValleys->GenUniformGrid2D(
            climate.peaksValleys.data(), originX, originZ,
            DensityField::kGridX, DensityField::kGridZ,
            freq::kPeaksValleys * kCellW, static_cast<int>(m_impl->seed) + 7919);
    }

    // 3D warp: 9 x 49 x 9 = 3,969 samples for a 393,216-voxel column.
    //
    // thread_local rather than a member: the graph is shared by every worker, so a
    // member scratch buffer would be a data race. Same reasoning as the mesher's.
    static thread_local std::vector<f32> warp;
    static thread_local std::vector<f32> cheeseField;
    warp.resize(DensityField::kSampleCount);
    cheeseField.resize(DensityField::kSampleCount);

    {
        MC_PROFILE_SCOPE_N("density3D");
        const i32 originY = kWorldMinY / DensityField::kCellHeight;
        m_impl->density3D->GenUniformGrid3D(
            warp.data(), originX, originY, originZ,
            DensityField::kGridX, DensityField::kGridY, DensityField::kGridZ,
            freq::kDensity3D * kCellW,
            static_cast<int>(m_impl->seed) + 4231);

        m_impl->cheese->GenUniformGrid3D(
            cheeseField.data(), originX, originY, originZ,
            DensityField::kGridX, DensityField::kGridY, DensityField::kGridZ,
            freq::kCheese * kCellW,
            static_cast<int>(m_impl->seed) + 5507);
    }

    // Combine into the final density, on the grid.
    {
        MC_PROFILE_SCOPE_N("shape");
        for (i32 gz = 0; gz < DensityField::kGridZ; ++gz) {
            for (i32 gx = 0; gx < DensityField::kGridX; ++gx) {
                const usize climateIndex = Climate::index(gx, gz);

                const f32 continents = continentSpline(climate.continentalness[climateIndex]);
                const f32 erosionFactor = erosionSpline(climate.erosion[climateIndex]);
                // Ridged noise is roughly [0, 1] rather than [-1, 1], so it is
                // recentred: without this every column would be pushed upward and the
                // base height would mean nothing.
                const f32 pv = climate.peaksValleys[climateIndex] * 2.0f - 1.0f;
                const f32 ridges = pv * erosionFactor;

                const f32 target = shape::kBaseHeight
                                 + continents * shape::kContinentAmplitude
                                 + ridges * shape::kRidgeAmplitude;

                for (i32 gy = 0; gy < DensityField::kGridY; ++gy) {
                    const auto worldY = static_cast<f32>(DensityField::gridYToWorldY(gy));

                    // Positive below the target height, negative above, crossing zero
                    // over kSqueeze blocks -- which is the band where the 3D field
                    // gets to decide and overhangs happen.
                    f32 value = (target - worldY) / shape::kSqueeze;
                    value += warp[DensityField::index(gx, gy, gz)] * shape::kWarpStrength;

                    // Cheese caverns: subtract density where the flattened field is
                    // strongest, but only well below the surface, so the terrain is not
                    // visibly perforated from above.
                    const f32 surfaceLimit =
                        target - static_cast<f32>(cave::kSurfaceMargin);
                    const auto floorLimit =
                        static_cast<f32>(shape::kBedrockTop + cave::kFloorMargin);
                    if (worldY < surfaceLimit && worldY > floorLimit) {
                        const f32 field = std::abs(cheeseField[DensityField::index(gx, gy, gz)]);
                        if (field > cave::kCheeseThreshold) {
                            const f32 excess = (field - cave::kCheeseThreshold)
                                             / (1.0f - cave::kCheeseThreshold);
                            value -= excess * cave::kCheeseStrength;
                        }
                    }

                    // Hard floor and a soft ceiling, so the world has a bottom and
                    // terrain never touches the build limit.
                    if (worldY <= static_cast<f32>(shape::kBedrockTop)) {
                        value = 1.0f;
                    } else if (worldY > shape::kFadeStartY) {
                        value -= (worldY - shape::kFadeStartY) * 0.1f;
                    }

                    density.at(gx, gy, gz) = value;
                }
            }
        }
    }
}

void DensityGraph::carveThinCaves(SectionPos pos, std::span<u8> solid) const {
    MC_PROFILE_SCOPE_N("DensityGraph::carveThinCaves");
    MC_ASSERT(solid.size() == kSectionVolume);

    const i32 baseX = pos.x * kSectionSize;
    const i32 baseY = pos.y * kSectionSize;
    const i32 baseZ = pos.z * kSectionSize;

    // Two SIMD grid calls for the whole section rather than a call per block. At block
    // resolution the grid step is one block, so the frequency needs no cell scaling.
    static thread_local std::vector<f32> spaghettiField;
    static thread_local std::vector<f32> noodleField;
    spaghettiField.resize(kSectionVolume);
    noodleField.resize(kSectionVolume);

    m_impl->spaghetti->GenUniformGrid3D(
        spaghettiField.data(), baseX, baseY, baseZ,
        kSectionSize, kSectionSize, kSectionSize,
        freq::kSpaghetti, static_cast<int>(m_impl->seed) + 8663);

    m_impl->noodle->GenUniformGrid3D(
        noodleField.data(), baseX, baseY, baseZ,
        kSectionSize, kSectionSize, kSectionSize,
        freq::kNoodle, static_cast<int>(m_impl->seed) + 9377);

    // FastNoise2 writes x fastest, then y, then z -- the same order localIndex() uses
    // for y and x but not for z, so the two indices are computed separately rather than
    // assumed equal.
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 y = 0; y < kSectionSize; ++y) {
            const i32 worldY = baseY + y;
            if (worldY < kCaveMinY || worldY > kCaveMaxY) {
                continue;
            }

            for (i32 x = 0; x < kSectionSize; ++x) {
                const usize voxel = localIndex(x, y, z);
                if (solid[voxel] == 0) {
                    continue; // Already air; nothing to carve.
                }

                const usize noiseIndex =
                    (static_cast<usize>(z) * kSectionSize + static_cast<usize>(y))
                        * kSectionSize
                    + static_cast<usize>(x);

                // A ridged field peaks at 1 along a surface through the volume. Taking
                // the blocks *near* that peak yields a tunnel whose width is set by the
                // threshold -- narrow for noodle, wider for spaghetti.
                const f32 spaghetti = 1.0f - spaghettiField[noiseIndex];
                const f32 noodle = 1.0f - noodleField[noiseIndex];

                if (spaghetti < cave::kSpaghettiWidth || noodle < cave::kNoodleWidth) {
                    solid[voxel] = 0;
                }
            }
        }
    }
}

} // namespace mc
