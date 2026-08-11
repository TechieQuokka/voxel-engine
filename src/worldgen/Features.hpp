#pragma once

#include "core/Types.hpp"
#include "world/Chunk.hpp"

namespace mc {

/// Places the blob features of `worldgen/FeatureTable.hpp` into a finished column.
///
/// **Runs last, and that is the whole reason ores mean anything.** Vanilla orders
/// generation `biomes -> noise -> surface -> carvers -> features`, and an ore's
/// air-exposure rule can only be evaluated once caves exist to expose it to. Run
/// before the carvers and the rule is dead code; run before the surface pass and
/// the surface would bury blobs it should have left alone.
///
/// **Seamless across columns by construction.** A blob whose centre sits near a
/// border overlaps its neighbour, so generating a column means replaying the
/// features of all nine columns in its 3x3 neighbourhood and keeping only the
/// blocks that land inside. That works because placement is a pure function of
/// (seed, column, feature, attempt) with no sequential RNG state -- a neighbour's
/// attempt number 40 is the same attempt whoever asks and in whatever order, so
/// the two columns agree about the blob they share without communicating.
///
/// Stateless for the same reason `Generator` is: const, writes only into the column
/// it is given, so N workers need no synchronisation.
class FeaturePlacer {
public:
    explicit FeaturePlacer(u32 seed) : m_seed(seed) {}

    void place(Chunk& chunk) const;

private:
    /// Replays one column's blob features, writing only what lands inside `target`.
    void placeFrom(Chunk& target, ChunkPos source) const;

    /// Plants this column's own trees. **No 3x3 replay**, unlike the blobs: a tree
    /// stands on the ground, so its height is a world read that a neighbour cannot
    /// make across a border. Trees are inset far enough from the column edge that
    /// they never cross one. See the note on `TreeSpec`.
    void placeTrees(Chunk& chunk) const;

    u32 m_seed;
};

} // namespace mc
