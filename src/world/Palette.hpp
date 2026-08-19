#pragma once

#include "core/BitPack.hpp"
#include "core/Types.hpp"

#include <optional>
#include <span>
#include <vector>

namespace mc {

/// Palette-compressed voxel storage.
///
/// Holds only the block types actually present, and stores one bit-packed index
/// per voxel. Terrain typically contains 4-16 distinct types per section, which
/// lands at 4 bits per voxel instead of 16 (DESIGN.md 3.5).
///
/// When a single block type fills the whole container the index array is not
/// allocated at all. Sky, bedrock and deep ocean floor all hit this case, which
/// is an estimated 60-70% of sections at the target render distance -- this is
/// what makes a 384-block world height affordable.
class Palette {
public:
    /// Creates a uniform container of `voxelCount` voxels, all `fill`.
    explicit Palette(usize voxelCount, BlockId fill = kAirBlock);

    /// Rebuilds a container from the parts `entries()` and `words()` handed out.
    ///
    /// **This is the load half of persistence, and it is the one entry point that
    /// takes its invariants from a file rather than from this class.** Every other
    /// mutator maintains them by construction; these bytes were on disk and may be
    /// truncated, reordered, or written by an older build. So it validates and
    /// returns nullopt rather than asserting: a corrupt save is a file to refuse,
    /// not a bug to abort on.
    ///
    /// The index scan is the expensive check and the one that matters. An index
    /// past the end of the palette would make `get` read off the end of the vector
    /// on the mesher's innermost loop, which is a use-after-free reached from
    /// bad data rather than from bad code.
    static std::optional<Palette> fromParts(usize voxelCount,
                                            std::span<const BlockId> entries,
                                            u32 bits,
                                            std::span<const u64> words);

    BlockId get(usize index) const {
        if (m_bits == 0) {
            return m_palette[0];
        }
        const u32 paletteIndex = bitpack::get(m_words.data(), index, m_bits);
        return m_palette[paletteIndex];
    }

    void set(usize index, BlockId block);

    /// Resets every voxel to `block` and collapses back to uniform storage.
    void fill(BlockId block);

    bool isUniform() const noexcept { return m_bits == 0; }

    /// Only meaningful when isUniform() is true.
    BlockId uniformBlock() const noexcept { return m_palette[0]; }

    u32 bitsPerIndex() const noexcept { return m_bits; }
    usize paletteSize() const noexcept { return m_palette.size(); }
    usize voxelCount() const noexcept { return m_voxelCount; }

    /// The distinct block types present, in palette-index order.
    ///
    /// Exposed for persistence, which writes these as *names* -- a BlockId is a
    /// position in the block table and moves whenever a block is added, so an id
    /// on disk would silently become a different block. See ChunkCodec.
    std::span<const BlockId> entries() const noexcept { return m_palette; }

    /// The packed index array. Empty when uniform.
    ///
    /// **Persistence writes these words through untouched, and that is the whole
    /// argument for not compressing the save file** (DESIGN.md 7.24): the words
    /// are already the compressed form, at 4 bits per voxel for typical terrain.
    std::span<const u64> words() const noexcept { return m_words; }

    /// Heap bytes held by this container, for memory accounting.
    usize memoryUsage() const noexcept;

    /// Drops palette entries no longer referenced and narrows the index width.
    ///
    /// Deliberately not called from set(): shrinking costs a full scan, so it
    /// is batched rather than paid on the edit path.
    void compact();

private:
    /// Linear scan. Palettes are 1-16 entries in practice, where scanning beats
    /// a hash map both in cache behaviour and in constant factor.
    i32 findInPalette(BlockId block) const;

    void growTo(u32 newBits);

    std::vector<BlockId> m_palette;
    std::vector<u64> m_words;
    usize m_voxelCount;
    u32 m_bits = 0;
};

} // namespace mc
