#pragma once

#include "world/Coords.hpp"
#include "world/Palette.hpp"

namespace mc {

/// A 32^3 block of voxels -- the unit of storage, meshing, culling and upload.
class Section {
public:
    explicit Section(BlockId fill = kAirBlock) : m_storage(kSectionVolume, fill) {}

    BlockId get(i32 x, i32 y, i32 z) const { return m_storage.get(localIndex(x, y, z)); }
    void set(i32 x, i32 y, i32 z, BlockId block) { m_storage.set(localIndex(x, y, z), block); }

    BlockId getByIndex(usize index) const { return m_storage.get(index); }

    void fill(BlockId block) { m_storage.fill(block); }

    /// True when one block type fills the section, so no index array exists.
    /// The mesher skips all-air sections outright on this check.
    bool isUniform() const noexcept { return m_storage.isUniform(); }
    BlockId uniformBlock() const noexcept { return m_storage.uniformBlock(); }

    bool isEmpty() const noexcept { return isUniform() && uniformBlock() == kAirBlock; }

    const Palette& storage() const noexcept { return m_storage; }
    Palette& storage() noexcept { return m_storage; }

    usize memoryUsage() const noexcept { return m_storage.memoryUsage(); }

private:
    Palette m_storage;
};

} // namespace mc
