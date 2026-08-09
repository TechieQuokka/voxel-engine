#pragma once

#include "core/Types.hpp"
#include "world/Coords.hpp"

#include <span>
#include <string_view>

namespace mc {

/// Texture array layers. The material field of a Quad holds one of these, not a
/// BlockId -- which is the whole point of using GL_TEXTURE_2D_ARRAY instead of
/// an atlas: a layer index needs no UV padding and merged quads tile it freely.
enum class TextureLayer : u16 {
    Stone = 0,
    Dirt = 1,
    GrassTop = 2,
    GrassSide = 3,
    Sand = 4,
    Count,
};

inline constexpr u16 kTextureLayerCount = static_cast<u16>(TextureLayer::Count);

/// Per-block-type properties consulted by the mesher.
struct BlockInfo {
    std::string_view name;
    /// Fully hides the neighbouring face. Air and (later) glass are not opaque.
    bool opaque = true;
    /// Placeholder surface colour, and the base colour the procedural texture
    /// for this block is generated from.
    u32 debugColor = 0xFFFFFFFFu;

    TextureLayer top = TextureLayer::Stone;
    TextureLayer side = TextureLayer::Stone;
    TextureLayer bottom = TextureLayer::Stone;
};

/// The set of block types known to the engine.
///
/// Static for now. It becomes data-driven when worldgen needs a real block
/// palette; the mesher only ever asks `isOpaque` and `textureLayer`, so that
/// change stays local.
class BlockRegistry {
public:
    static const BlockRegistry& instance();

    const BlockInfo& operator[](BlockId id) const;

    bool isOpaque(BlockId id) const { return (*this)[id].opaque; }

    /// Texture array layer for one face of a block.
    u16 textureLayer(BlockId id, Face face) const;

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
