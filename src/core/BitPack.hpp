#pragma once

#include "core/Assert.hpp"
#include "core/Types.hpp"

#include <bit>

namespace mc {

/// Bit-packed index storage for palette-compressed sections.
///
/// Only widths 1, 2, 4 and 8 are used. That is a deliberate restriction: every
/// one of them divides 64 evenly, so an index can never straddle a word
/// boundary. Reading is a shift and a mask with no carry handling, which
/// matters because these accessors sit in the innermost loop of the mesher.
///
/// A width of 0 means the section is uniform and stores no index array at all.
namespace bitpack {

inline constexpr u32 kMaxBits = 8;

/// Smallest supported width that can address `paletteSize` distinct entries.
constexpr u32 bitsForPaletteSize(usize paletteSize) {
    if (paletteSize <= 1) {
        return 0;
    }
    if (paletteSize <= 2) {
        return 1;
    }
    if (paletteSize <= 4) {
        return 2;
    }
    if (paletteSize <= 16) {
        return 4;
    }
    MC_ASSERT_MSG(paletteSize <= 256, "palette larger than 8-bit indices allow");
    return 8;
}

constexpr bool isValidWidth(u32 bits) {
    return bits == 0 || bits == 1 || bits == 2 || bits == 4 || bits == 8;
}

constexpr u32 indicesPerWord(u32 bits) {
    MC_ASSERT(bits != 0);
    return 64u / bits;
}

/// Number of u64 words needed to hold `count` indices of the given width.
constexpr usize wordsNeeded(usize count, u32 bits) {
    if (bits == 0) {
        return 0;
    }
    const auto perWord = static_cast<usize>(indicesPerWord(bits));
    return (count + perWord - 1) / perWord;
}

constexpr u64 mask(u32 bits) {
    MC_ASSERT(bits != 0 && bits <= kMaxBits);
    return (u64{1} << bits) - 1;
}

constexpr u32 get(const u64* words, usize index, u32 bits) {
    MC_ASSERT(bits != 0);
    const usize perWord = indicesPerWord(bits);
    const usize word = index / perWord;
    const u32 shift = static_cast<u32>(index % perWord) * bits;
    return static_cast<u32>((words[word] >> shift) & mask(bits));
}

constexpr void set(u64* words, usize index, u32 bits, u32 value) {
    MC_ASSERT(bits != 0);
    MC_ASSERT(value <= mask(bits));
    const usize perWord = indicesPerWord(bits);
    const usize word = index / perWord;
    const u32 shift = static_cast<u32>(index % perWord) * bits;
    words[word] = (words[word] & ~(mask(bits) << shift)) | (static_cast<u64>(value) << shift);
}

} // namespace bitpack
} // namespace mc
