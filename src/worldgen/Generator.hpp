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

    /// Highest solid block in a column, or kWorldMinY - 1 if there is none.
    ///
    /// Costs a full column evaluation, so this is for tests and tools, not for
    /// generation — the surface pass finds the surface as it goes.
    i32 surfaceHeight(i32 worldX, i32 worldZ) const;

    u32 seed() const noexcept { return m_seed; }
    const DensityGraph& graph() const noexcept { return *m_graph; }

private:
    /// Depth of soil under the surface block; stone below that.
    static constexpr i32 kSoilDepth = 4;
    /// At or below this height the surface is sand rather than grass — a stand-in
    /// for a beach until biomes exist.
    static constexpr i32 kBeachLevel = 66;

    u32 m_seed;
    std::unique_ptr<DensityGraph> m_graph;
};

} // namespace mc
