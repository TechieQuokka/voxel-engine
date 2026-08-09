#include "world/Palette.hpp"

#include "core/Assert.hpp"

#include <algorithm>

namespace mc {

Palette::Palette(usize voxelCount, BlockId fill) : m_voxelCount(voxelCount) {
    MC_VERIFY(voxelCount > 0);
    m_palette.push_back(fill);
}

i32 Palette::findInPalette(BlockId block) const {
    for (usize i = 0; i < m_palette.size(); ++i) {
        if (m_palette[i] == block) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

void Palette::growTo(u32 newBits) {
    MC_ASSERT(newBits > m_bits);
    MC_ASSERT(bitpack::isValidWidth(newBits));

    std::vector<u64> widened(bitpack::wordsNeeded(m_voxelCount, newBits), 0);

    if (m_bits != 0) {
        for (usize i = 0; i < m_voxelCount; ++i) {
            const u32 value = bitpack::get(m_words.data(), i, m_bits);
            bitpack::set(widened.data(), i, newBits, value);
        }
    }
    // When m_bits was 0 every voxel is palette entry 0, and `widened` is
    // already zero-filled, so there is nothing to copy.

    m_words = std::move(widened);
    m_bits = newBits;
}

void Palette::set(usize index, BlockId block) {
    MC_ASSERT(index < m_voxelCount);

    if (m_bits == 0 && m_palette[0] == block) {
        return; // Uniform container, unchanged.
    }

    i32 paletteIndex = findInPalette(block);
    if (paletteIndex < 0) {
        m_palette.push_back(block);
        paletteIndex = static_cast<i32>(m_palette.size()) - 1;

        const u32 required = bitpack::bitsForPaletteSize(m_palette.size());
        if (required > m_bits) {
            growTo(required);
        }
    }

    bitpack::set(m_words.data(), index, m_bits, static_cast<u32>(paletteIndex));
}

void Palette::fill(BlockId block) {
    // Both allocations are released, not just the index array. clear() alone
    // would keep the palette's grown capacity, and at ~38,600 sections a
    // retained 64-byte palette per section is megabytes of nothing.
    m_palette.clear();
    m_palette.shrink_to_fit();
    m_palette.push_back(block);

    m_words.clear();
    m_words.shrink_to_fit();

    m_bits = 0;
}

usize Palette::memoryUsage() const noexcept {
    return m_palette.capacity() * sizeof(BlockId) + m_words.capacity() * sizeof(u64);
}

void Palette::compact() {
    if (m_bits == 0) {
        return;
    }

    // Which palette entries are still referenced?
    std::vector<bool> used(m_palette.size(), false);
    for (usize i = 0; i < m_voxelCount; ++i) {
        used[bitpack::get(m_words.data(), i, m_bits)] = true;
    }

    // Build the compacted palette and a remap table in one pass.
    std::vector<BlockId> compacted;
    std::vector<u32> remap(m_palette.size(), 0);
    for (usize i = 0; i < m_palette.size(); ++i) {
        if (used[i]) {
            remap[i] = static_cast<u32>(compacted.size());
            compacted.push_back(m_palette[i]);
        }
    }

    if (compacted.size() == m_palette.size()) {
        return; // Nothing to reclaim.
    }

    const u32 newBits = bitpack::bitsForPaletteSize(compacted.size());
    if (newBits == 0) {
        m_palette = std::move(compacted);
        m_words.clear();
        m_words.shrink_to_fit();
        m_bits = 0;
        return;
    }

    std::vector<u64> rewritten(bitpack::wordsNeeded(m_voxelCount, newBits), 0);
    for (usize i = 0; i < m_voxelCount; ++i) {
        const u32 oldIndex = bitpack::get(m_words.data(), i, m_bits);
        bitpack::set(rewritten.data(), i, newBits, remap[oldIndex]);
    }

    m_palette = std::move(compacted);
    m_words = std::move(rewritten);
    m_bits = newBits;
}

} // namespace mc
