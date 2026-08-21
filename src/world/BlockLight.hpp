#pragma once

#include "world/Coords.hpp"

#include <vector>

namespace mc {

class Chunk;
class World;

/// Every section whose stored block light an update actually changed.
///
/// **Across columns, which is the whole reason this type exists.** Sky light reports
/// its changes as a bitmask over one column, because `computeSkyLight` cannot leave
/// the column it was handed. Block light can and must: a torch reaches fifteen blocks
/// and a column is thirty-two wide, so a torch has to sit within a 3x3 patch at the
/// exact centre of a column for its light not to cross a wall. That is nine positions
/// out of a thousand and twenty-four. A per-column mask cannot describe the answer.
struct LightTouch {
    std::vector<SectionPos> sections;

    void clear() { sections.clear(); }
    bool empty() const noexcept { return sections.empty(); }
};

/// Brings block light back in line after one block at `pos` changed from `before`
/// to `after`.
///
/// **World space, through `World`, and that is what makes column boundaries cost
/// nothing.** The sky light pass works on a flat buffer covering exactly one column
/// and documents the seam at its walls as acceptable, which it is: the vertical fill
/// is exact, so only cave interiors near a border come out dim. Block light has no
/// such excuse. A player puts a torch where a player wants a torch, the error would
/// be a straight vertical line of darkness through the middle of a lit room, and it
/// would happen almost every time. So this reads and writes through the loaded set
/// instead of through a buffer, and a neighbouring column is simply the next lookup.
///
/// Four cases, all of them handled by the same two passes:
///
/// - **An emitter appears.** Add from it.
/// - **An emitter goes away.** Remove what it was giving, then re-add from whatever
///   survived at the edges of the hole -- that is what stops a second torch nearby
///   from being switched off by the first one breaking.
/// - **An opaque block appears.** Remove what stood in that cell, and the shadow is
///   the hole that leaves.
/// - **An opaque block goes away.** Add from its six neighbours, which is how light
///   pours into a room the moment the wall opens.
///
/// A change that is none of those -- dirt to gravel, say -- returns having done
/// nothing, and that is the common case on the digging path.
///
/// **Main thread only.** It writes light into as many as nine columns, and
/// `LightArray::set` reallocates when it leaves its uniform state, so a meshing job
/// reading one of them would be a use-after-free rather than a torn read. The pin
/// check that makes this safe lives in `World::setBlock`, which is the only caller
/// that edits.
///
/// Unloaded and still-generating columns read as opaque, so light stops at the edge
/// of the loaded region rather than leaking into a column that has not arrived. The
/// column is remeshed when it does arrive, and `seedBlockLight` relights it then.
void updateBlockLight(World& world, BlockPos pos, BlockId before, BlockId after,
                      LightTouch& touched);

/// Whether `updateBlockLight` could write anything at all. Seven light reads and no
/// flood.
///
/// **This exists to keep the pin check off the digging path.** The flood writes into
/// as many as nine columns, so an edit that could move light has to find all nine
/// unpinned or come back later -- and asking that on every click would put a `Busy`
/// retry in front of ordinary digging, because breaking any solid block changes
/// whether that cell blocks light and so cannot be ruled out on block types alone.
///
/// What *can* be ruled out is a neighbourhood with no block light in it. A cell holds
/// nothing, emits nothing and touches nothing lit, so nothing can move and the whole
/// pass is skipped. In a world where nobody has placed a torch that is every edit
/// there is, which is what makes torches cost nothing until they exist.
bool blockLightCanMove(const World& world, BlockPos pos, BlockId before, BlockId after);

/// Answers `Chunk::hasEmitter` for a column that has just been filled, and records it.
///
/// **Asked of each section's palette, not of its voxels.** A section holds 32,768
/// voxels and names one to sixteen block types; a torch that is not in the palette is
/// not in the section. Called from the worker that generated or loaded the column,
/// before it is published as Ready.
void noteEmitters(Chunk& chunk);

/// Relights `column` from every emitter in it and in its eight neighbours.
///
/// **Block light is derived, so it is not saved.** A column comes back from disk
/// with its torches but with no light around them, exactly as a freshly generated
/// column does -- and a torch one block from a border also owes light to the column
/// next door, which may have loaded first. Seeding from the neighbours as well as
/// from the column itself covers both directions.
///
/// Idempotent, because the add pass only ever raises a cell: re-seeding a region
/// that is already correct walks it once and changes nothing. That is what makes it
/// safe to call on every column that becomes ready, without tracking which of its
/// neighbours have already been done.
void seedBlockLight(World& world, ChunkPos column, LightTouch& touched);

} // namespace mc
