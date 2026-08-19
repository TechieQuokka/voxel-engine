#pragma once

#include "core/Result.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"
#include "world/Furnace.hpp"
#include "world/ItemStack.hpp"

#include <array>
#include <span>
#include <vector>

namespace mc {

class Chunk;

/// A furnace's contents and the block it sits in, as they go to disk.
///
/// **Furnaces travel with their column rather than in a file of their own.** A
/// furnace is Minecraft's block entity: the half of a block's state that does not
/// fit in a palette index. It is created and destroyed with its block, it is only
/// ever reachable through the column that holds it, and unloading that column is
/// exactly when it has to be written. A separate store would need its own index
/// keyed by the same position and would be able to disagree with the world about
/// whether a furnace is there.
///
/// **Plain data rather than a `Furnace`, because a `Container` is deliberately
/// neither copyable nor movable.** That is a statement about identity -- the
/// furnace the player is looking at is *the* furnace, not a value that can be in
/// two places -- and a save record is the opposite thing: a value, in a vector,
/// with no identity until `applyFurnace` gives it one.
struct SavedFurnace {
    BlockPos position;
    Furnace::Timers timers;
    std::array<ItemStack, Furnace::kSlots> slots{};
};

/// Reads a live furnace into a save record.
SavedFurnace captureFurnace(BlockPos position, const Furnace& furnace);

/// Writes a save record back into a live furnace, clamping what it must.
void applyFurnace(const SavedFurnace& saved, Furnace& furnace);

/// A column's voxels, as bytes on disk and back.
///
/// **A full snapshot of the column, not a delta against generation.** Saving only
/// the blocks the player changed would cost about 120 bytes where this costs tens
/// of kilobytes, and the arithmetic is tempting: generation is deterministic, so a
/// delta plus a seed reconstructs the column exactly. It reconstructs it exactly
/// *for the generator that wrote it*. Phase 4d (biomes) is open and will move
/// terrain everywhere, at which point every delta on disk would replay onto ground
/// that is no longer there -- a house half-buried, a mine ending in rock, and no
/// error anywhere to say why. Minecraft stores whole chunks for this reason; here
/// it is not a hypothetical but a scheduled change. See DESIGN.md 7.24.
///
/// ## Layout
///
/// Little-endian throughout, which is a statement about x86-64 rather than a
/// choice -- this project is Linux and GCC on one architecture by constraint
/// (DESIGN.md 1), so a byte-swapping reader would be untestable code guarding
/// against a port that is ruled out.
///
/// ```text
///   u32  magic          'MCCL'
///   u16  version        kChunkFormatVersion
///   u16  sectionCount   always kSectionsPerColumn
///   per section, bottom to top:
///     u16   paletteSize
///     per palette entry:
///       u16   nameLength
///       char  name[nameLength]      not NUL-terminated
///     u8    bitsPerIndex            0 when the section is uniform
///     u32   wordCount
///     u64   words[wordCount]
///   u16  furnaceCount
///   per furnace:
///     i32   x, y, z                 world coordinates
///     u32   burnRemaining, burnTotal, cookTicks
///     per slot, ingredient/fuel/output:
///       u16   nameLength            0 for an empty slot
///       char  name[nameLength]
///       u32   count
/// ```
///
/// **Palette entries are names, not ids.** A `BlockId` is a position in `kBlocks`,
/// so inserting one block type shifts every id above it and would turn saved stone
/// into deepslate with nothing to detect it. Vanilla writes namespaced ids in its
/// chunk palettes for exactly this reason.
///
/// **Sky light is absent on purpose.** It is derived from the voxels by
/// `computeSkyLight`, costs about 0.5 ms for a column, and would otherwise be the
/// largest thing in the file -- a nibble per voxel is 16 KiB per non-uniform
/// section against the 16 KiB the blocks themselves take at 4 bits. Decoding
/// recomputes it, which also means a save written before a lighting fix picks the
/// fix up rather than preserving the bug.

/// Bumped whenever the layout above changes. A file with any other version is
/// refused rather than guessed at, and the column regenerates.
inline constexpr u16 kChunkFormatVersion = 1;

enum class ChunkDecodeError {
    /// Not a column payload at all.
    BadMagic,
    /// Written by a build whose layout this one does not know.
    BadVersion,
    /// The payload ends in the middle of a field.
    Truncated,
    /// A section count, palette size or index width that cannot be right.
    Malformed,
    /// A palette that `Palette::fromParts` refused -- most often an index pointing
    /// past the end of its own palette.
    BadPalette,
};

const char* describe(ChunkDecodeError error);

/// Serializes every section of `chunk`, plus the furnaces standing in it.
///
/// Never fails: the column is in memory and its invariants already hold.
std::vector<u8> encodeChunk(const Chunk& chunk,
                            std::span<const SavedFurnace> furnaces = {});

/// Fills `chunk`'s voxels from `bytes` and recomputes its sky light.
///
/// Leaves the column's state and dirty mask alone, exactly as the generator's
/// stages do before its own tail -- the caller sets `Ready` and marks dirty, so
/// that a loaded column and a generated one arrive by the same steps.
///
/// **On failure `chunk` may hold half a column, and that is safe.** The only
/// caller regenerates when this fails, and every branch of `Generator::generateColumn`
/// begins by filling the section it is about to write.
///
/// A palette entry naming a block this build does not have becomes air, with a
/// warning, rather than failing the column. That is the one case where a save from
/// a newer build stays partly readable.
///
/// `furnaces`, when given, is cleared and filled with what the column held. Null
/// when the caller does not keep furnaces -- the trailer is still parsed, because
/// skipping it would mean trusting a length this function has not read.
Result<void, ChunkDecodeError> decodeChunk(std::span<const u8> bytes, Chunk& chunk,
                                           std::vector<SavedFurnace>* furnaces = nullptr);

} // namespace mc
