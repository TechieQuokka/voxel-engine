#pragma once

#include "core/Types.hpp"

#include <span>
#include <string_view>

namespace mc {

/// Per-block-type properties consulted by the mesher.
struct BlockInfo {
    std::string_view name;
    /// Fully hides the neighbouring face. Air and (later) glass are not opaque.
    bool opaque = true;
    /// Placeholder surface colour until the texture array lands in Phase 2.
    u32 debugColor = 0xFFFFFFFFu;
};

/// The set of block types known to the engine.
///
/// Static for now. It becomes data-driven when worldgen needs a real block
/// palette; the mesher only ever asks `isOpaque`, so that change stays local.
class BlockRegistry {
public:
    static const BlockRegistry& instance();

    const BlockInfo& operator[](BlockId id) const;

    bool isOpaque(BlockId id) const { return (*this)[id].opaque; }

    usize size() const { return m_blocks.size(); }

    std::span<const BlockInfo> all() const { return m_blocks; }

private:
    BlockRegistry();

    std::span<const BlockInfo> m_blocks;
};

// Block ids used in Phase 1. kAirBlock (0) is declared in core/Types.hpp.
inline constexpr BlockId kStoneBlock = 1;
inline constexpr BlockId kDirtBlock  = 2;
inline constexpr BlockId kGrassBlock = 3;
inline constexpr BlockId kSandBlock  = 4;

} // namespace mc
