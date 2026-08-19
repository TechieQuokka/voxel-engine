#pragma once

#include "core/Types.hpp"
#include "world/BlockTable.hpp"
#include "world/Coords.hpp"

#include <optional>
#include <span>
#include <string_view>

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
    bool isFluid(BlockId id) const { return (*this)[id].fluid; }

    /// Texture array layer for one face of a block.
    u16 textureLayer(BlockId id, Face face) const;

    /// Resolves a block's name to its id, or nullopt if no block has that name.
    ///
    /// The runtime counterpart of `blockIdOf`, which is consteval and therefore
    /// unavailable to a name that arrives at runtime. Persistence is the only
    /// caller and needs exactly this: a save file stores palette entries by name,
    /// because a BlockId is a position in `kBlocks` and adding one block would
    /// otherwise turn every saved stone into deepslate.
    ///
    /// **Nullopt is a real answer, not an error to assert on.** A save written by a
    /// build that had a block this one does not is the case, and the loader turns
    /// it into air with a warning rather than refusing the column.
    ///
    /// Linear over ~40 entries, called once per palette entry per column load.
    std::optional<BlockId> findByName(std::string_view name) const;

    usize size() const { return m_blocks.size(); }

    std::span<const BlockInfo> all() const { return m_blocks; }

private:
    BlockRegistry();

    std::span<const BlockInfo> m_blocks;
};

} // namespace mc
