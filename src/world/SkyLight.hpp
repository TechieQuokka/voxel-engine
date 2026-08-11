#pragma once

#include "core/Types.hpp"

namespace mc {

class Chunk;

/// Fills in a column's sky light.
///
/// Two rules, both Minecraft's. Light falls straight down through transparent
/// blocks without weakening, so a shaft in a cave roof stays bright all the way to
/// the floor. In every other direction it drops by one per block, which is what
/// makes a cave mouth fade into darkness over about fifteen blocks instead of
/// stopping at a line.
///
/// **Column-local, and the seam that costs is documented rather than hidden.**
/// Light does not cross into the neighbouring column, so a cave lit through an
/// opening one column over stays dark, and the boundary between them is a straight
/// vertical edge. The vertical fill is exact -- it depends only on this column's own
/// heightmap -- so open sky, the surface and everything above it are unaffected;
/// the error is confined to cave interiors within about fifteen blocks of a border.
///
/// Fixing it properly means propagating between columns once neighbours exist,
/// which needs a light-changed signal into the same dirty-mask and pin machinery
/// meshing uses. That is a phase, not a patch, and doing it wrong would corrupt
/// meshes rather than just dim them.
///
/// Runs after features, because what casts a shadow depends on what is there.
///
/// **Returns one bit per section whose stored light this call actually changed**,
/// in the same order as `Chunk`'s dirty mask. Generation ignores it -- a freshly
/// generated column marks everything dirty regardless -- but editing a block needs
/// it: a light change has to be remeshed, and remeshing all twelve sections of a
/// column plus its neighbours' boundaries on every click, most of them unchanged,
/// is the difference between a dig that stutters and one that does not. Breaking a
/// block deep underground changes no sky light at all, and this is what says so.
u16 computeSkyLight(Chunk& chunk);

} // namespace mc
