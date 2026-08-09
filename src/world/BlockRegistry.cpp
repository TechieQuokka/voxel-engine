#include "world/BlockRegistry.hpp"

#include "core/Assert.hpp"

#include <array>

namespace mc {
namespace {

// Indexed by BlockId; order must match the k*Block constants.
constexpr std::array<BlockInfo, 5> kBlocks{{
    {"air",   false, 0x00000000u,
     TextureLayer::Stone, TextureLayer::Stone, TextureLayer::Stone},
    {"stone", true,  0xFF8C8C8Cu,
     TextureLayer::Stone, TextureLayer::Stone, TextureLayer::Stone},
    {"dirt",  true,  0xFF6B4A2Fu,
     TextureLayer::Dirt, TextureLayer::Dirt, TextureLayer::Dirt},
    // Grass is the reason faces carry a layer rather than a block id: three
    // different textures on one block type.
    {"grass", true,  0xFF5FA341u,
     TextureLayer::GrassTop, TextureLayer::GrassSide, TextureLayer::Dirt},
    {"sand",  true,  0xFFD8CA8Cu,
     TextureLayer::Sand, TextureLayer::Sand, TextureLayer::Sand},
}};

} // namespace

BlockRegistry::BlockRegistry() : m_blocks(kBlocks) {}

const BlockRegistry& BlockRegistry::instance() {
    static const BlockRegistry registry;
    return registry;
}

const BlockInfo& BlockRegistry::operator[](BlockId id) const {
    MC_ASSERT_MSG(id < m_blocks.size(), "unknown BlockId");
    return m_blocks[id];
}

u16 BlockRegistry::textureLayer(BlockId id, Face face) const {
    const BlockInfo& info = (*this)[id];
    switch (face) {
    case Face::PosY: return static_cast<u16>(info.top);
    case Face::NegY: return static_cast<u16>(info.bottom);
    default:         return static_cast<u16>(info.side);
    }
}

} // namespace mc
