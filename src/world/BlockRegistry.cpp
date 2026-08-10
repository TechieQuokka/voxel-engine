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

u16 BlockRegistry::textureLayer(BlockId id, Face face) const {
    const BlockInfo& info = (*this)[id];
    switch (face) {
    case Face::PosY: return info.top;
    case Face::NegY: return info.bottom;
    default:         return info.side;
    }
}

} // namespace mc
