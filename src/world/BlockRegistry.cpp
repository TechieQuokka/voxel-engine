#include "world/BlockRegistry.hpp"

#include "core/Assert.hpp"

namespace mc {

BlockRegistry::BlockRegistry() : m_blocks(kBlocks) {}

const BlockRegistry& BlockRegistry::instance() {
    static const BlockRegistry registry;
    return registry;
}

const BlockInfo& BlockRegistry::operator[](BlockId id) const {
    MC_ASSERT_MSG(id < m_blocks.size(), "unknown BlockId");
    return m_blocks[id];
}

std::optional<BlockId> BlockRegistry::findByName(std::string_view name) const {
    for (usize i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].name == name) {
            return static_cast<BlockId>(i);
        }
    }
    return std::nullopt;
}

u16 BlockRegistry::textureLayer(BlockId id, Face face) const {
    const BlockInfo& info = (*this)[id];
    switch (face) {
    case Face::PosY: return info.top;
    case Face::NegY: return info.bottom;
    default:         return info.side;
    }
}

} // namespace mc
