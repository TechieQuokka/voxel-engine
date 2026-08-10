#pragma once

#include "core/Types.hpp"
#include "world/Chunk.hpp"
#include "worldgen/DensityGraph.hpp"

#include <memory>

namespace mc {

/// Fills a chunk column with terrain.
///
/// Follows the order Minecraft's chunk pipeline uses, because the order is load
/// bearing rather than incidental (see DESIGN.md 7.6):
///
///   1. **noise**   — the density field decides solid or air, nothing else
///   2. **surface** — biome-dependent blocks replace the top of the solid column
///
/// Carvers and features (caves, ores) come next; they have to run after `noise`
/// because they replace blocks it placed, and ore's air-exposure rule only means
/// anything once caves exist to expose it to.
///
/// One call fills one column, reads nothing but the shared noise graph, and writes
/// nothing but that column — which is what makes it safe on N workers with no
/// synchronisation at all.
class Generator {
public:
    explicit Generator(u32 seed = 1337u);
    ~Generator();

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    /// Thread-safe by construction: const, and writes only into `chunk`.
    void generateColumn(Chunk& chunk) const;

    /// Highest solid block according to the **density field alone** — carvers ignored.
    ///
    /// This is the terrain's shape: what the shaper produced before caves cut into it.
    /// Cheap, because it needs only the interpolation grid. Use it for anything about
    /// landscape form — height ranges, relief, continuity across a seam.
    ///
    /// It is deliberately *not* the height of the finished world. A cave mouth does not
    /// lower it.
    i32 terrainHeight(i32 worldX, i32 worldZ) const;

    /// Highest solid block in the **finished** column, carvers included.
    ///
    /// Truthful and expensive: it generates the column, because thin caves are cut per
    /// block after the density field and cannot be read off it. The last column is
    /// cached per thread, since callers sweep many positions within one — without that a
    /// 32x32 sweep would generate the same column 1,024 times.
    ///
    /// Use this where the answer has to match what a player would stand on. Use
    /// terrainHeight where it does not, or the cost is wasted.
    i32 surfaceHeight(i32 worldX, i32 worldZ) const;

    u32 seed() const noexcept { return m_seed; }
    const DensityGraph& graph() const noexcept { return *m_graph; }

private:
    /// Depth of soil under the surface block; stone below that.
    static constexpr i32 kSoilDepth = 4;
    /// At or below this height the surface is sand rather than grass — a stand-in
    /// for a beach until biomes exist.
    static constexpr i32 kBeachLevel = 66;

    /// How far below the density field's terrain top the surface rule still applies.
    ///
    /// Wide enough to catch an overhang's shelf, narrow enough to exclude a cave
    /// ceiling. Without this bound the rule fires on every solid run in the column,
    /// which put grass and sand on the roof of every cave in the world.
    static constexpr i32 kSurfaceBand = 6;

    u32 m_seed;
    std::unique_ptr<DensityGraph> m_graph;
};

} // namespace mc
