#pragma once

#include "core/Assert.hpp"
#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <vector>

namespace mc {

/// One light channel over a section: 4 bits per voxel, collapsed when uniform.
///
/// **The collapse is not an optimisation here, it is the feasibility argument.** A
/// 32^3 section holds 32,768 voxels; at a nibble each that is 16 KiB per channel,
/// 192 KiB per column, and over 200 MiB at render distance 16 -- comparable to the
/// entire mesh arena, for one channel. What makes it affordable is that almost
/// every section is uniform: everything above the terrain is sky light 15 all
/// through, and everything below the reach of a cave is 0. Only sections holding
/// the surface, or a cave that daylight gets into, ever allocate.
///
/// Same shape as `Palette`, deliberately, and for the same reason -- so the two
/// answer "does this section cost anything?" the same way.
class LightArray {
public:
    /// Uniform `level` everywhere, allocating nothing.
    explicit LightArray(u8 level = 0) : m_uniform(level) {}

    u8 get(usize index) const {
        MC_ASSERT(index < kSectionVolume);
        if (m_nibbles.empty()) {
            return m_uniform;
        }
        const u8 packed = m_nibbles[index >> 1];
        return (index & 1u) != 0 ? static_cast<u8>(packed >> 4)
                                 : static_cast<u8>(packed & 0x0Fu);
    }

    u8 get(i32 x, i32 y, i32 z) const { return get(localIndex(x, y, z)); }

    void set(usize index, u8 level) {
        MC_ASSERT(index < kSectionVolume);
        MC_ASSERT(level <= 15);

        if (m_nibbles.empty()) {
            if (level == m_uniform) {
                return; // Still uniform; nothing to store.
            }
            expand();
        }

        u8& packed = m_nibbles[index >> 1];
        if ((index & 1u) != 0) {
            packed = static_cast<u8>((packed & 0x0Fu) | static_cast<u8>(level << 4));
        } else {
            packed = static_cast<u8>((packed & 0xF0u) | level);
        }
    }

    void set(i32 x, i32 y, i32 z, u8 level) { set(localIndex(x, y, z), level); }

    /// Resets to uniform `level`, releasing the nibble array.
    void fill(u8 level) {
        MC_ASSERT(level <= 15);
        m_uniform = level;
        m_nibbles.clear();
        m_nibbles.shrink_to_fit();
    }

    bool isUniform() const noexcept { return m_nibbles.empty(); }
    /// Only meaningful when isUniform().
    u8 uniformLevel() const noexcept { return m_uniform; }

    usize memoryUsage() const noexcept { return m_nibbles.capacity(); }

private:
    void expand() {
        const u8 pair = static_cast<u8>(m_uniform | (m_uniform << 4));
        m_nibbles.assign(kSectionVolume / 2, pair);
    }

    std::vector<u8> m_nibbles;
    u8 m_uniform = 0;
};

static_assert(kSectionVolume % 2 == 0, "two voxels share a byte");

} // namespace mc
