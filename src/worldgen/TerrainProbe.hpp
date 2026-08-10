#pragma once

#include "core/Types.hpp"

namespace mc {

class Generator;

/// Reports what the terrain is actually made of, by generating columns and counting.
///
/// **This exists because the things being tuned are underground.** Caves were tuned
/// against a measured air fraction and a printed cross-section rather than a
/// screenshot, for the reason recorded in HANDOFF section 5: from above, a world
/// with no caves looks identical to a world full of them. Deepslate, stone variants
/// and ores are all worse than caves in that respect -- none of them are visible
/// from any camera position a player starts at, so "it looks right" is not available
/// as evidence and a composition readout is the only honest check.
///
/// Needs no GL and no window, so it runs before the engine starts.
struct ProbeOptions {
    /// Columns to generate and count. Sampled along a diagonal so the sweep crosses
    /// varied continentalness rather than measuring one biome-shaped patch.
    i32 columns = 24;
    /// Print a vertical slice of one column, as glyphs from the block table.
    bool crossSection = true;
    /// Y range of the printed slice. Defaults to the band the deepslate transition
    /// and the ore-bearing depths share.
    i32 sliceMinY = -64;
    i32 sliceMaxY = 24;
};

void runTerrainProbe(const Generator& generator, const ProbeOptions& options);

} // namespace mc
