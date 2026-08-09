#pragma once

#include "core/BitPack.hpp"
#include "core/Types.hpp"

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
