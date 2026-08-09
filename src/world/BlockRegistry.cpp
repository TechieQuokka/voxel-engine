#include "world/BlockRegistry.hpp"

#include "core/Assert.hpp"

#include <array>

namespace mc {
namespace {

// Indexed by BlockId; order must match the k*Block constants.
constexpr std::array<BlockInfo, 5> kBlocks{{
    {"air",   false, 0x00000000u},
    {"stone", true,  0xFF8C8C8Cu},
    {"dirt",  true,  0xFF6B4A2Fu},
    {"grass", true,  0xFF5FA341u},
    {"sand",  true,  0xFFD8CA8Cu},
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

} // namespace mc
