#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"
#include "world/Coords.hpp"

#include <span>

namespace mc {

/// Read access to the block table.
///
/// The data itself lives in `world/BlockTable.hpp`, which is the file to edit when
/// adding a block type. This is only the lookup: it exists so that the mesher asks
/// a question ("is this opaque?", "which layer does this face draw?") rather than
/// indexing a table, which is what will let the table become data-driven later
/// without touching a single caller.
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

} // namespace mc
