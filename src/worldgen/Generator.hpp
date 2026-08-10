#pragma once

#include "core/Types.hpp"
#include "world/Chunk.hpp"

namespace mc {

/// Fills a chunk column with terrain.
///
/// **Placeholder for Phase 3.** The body is a deterministic analytic heightmap in
/// world coordinates, which is all streaming needs to be exercised: it is
/// continuous across chunk boundaries, so seams are visible if the neighbour
/// handling is wrong, and it is cheap enough that Phase 3's timings measure the
/// streaming machinery rather than the noise.
///
/// Phase 4 replaces the body with the FastNoise2 density graph of DESIGN.md 3.12.
/// What does not change is the shape of the interface, which is what the worker
/// pool requires: one call fills one column, reads nothing but its own parameters,
/// and writes nothing but that column. That makes it safe to run on N threads with
/// no synchronisation at all.
class Generator {
public:
    explicit Generator(u32 seed = 1337u);

    /// Thread-safe by construction — const, and writes only into `chunk`.
    void generateColumn(Chunk& chunk) const;

    /// Highest solid block at a world column position.
    i32 surfaceHeight(i32 worldX, i32 worldZ) const;

    u32 seed() const noexcept { return m_seed; }

private:
    /// Sea-level-ish reference height the terrain varies around.
    static constexpr i32 kBaseHeight = 40;
    /// Depth of soil under the surface block; stone below that.
    static constexpr i32 kSoilDepth = 4;
    /// At or below this height the surface is sand rather than grass, which gives
    /// the palette a fourth block type to compress and makes low ground read
    /// differently from high ground.
    static constexpr i32 kBeachLevel = 34;

    static BlockId blockFor(i32 y, i32 surface);

    u32 m_seed;
    f32 m_offsetX;
    f32 m_offsetZ;
};

} // namespace mc
