#pragma once

#include "world/Coords.hpp"
#include "world/LightArray.hpp"
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

    /// Sky light, 0 to 15.
    ///
    /// Only sky for now. Block light is a second `LightArray` and the propagation
    /// is the same code, but nothing in the world emits light yet -- there are no
    /// torches and lava is not a block type -- so a block channel would be a
    /// uniformly zero array in every section in the world.
    u8 skyLight(i32 x, i32 y, i32 z) const { return m_skyLight.get(x, y, z); }
    void setSkyLight(i32 x, i32 y, i32 z, u8 level) { m_skyLight.set(x, y, z, level); }

    const LightArray& skyLightArray() const noexcept { return m_skyLight; }
    LightArray& skyLightArray() noexcept { return m_skyLight; }

    usize memoryUsage() const noexcept {
        return m_storage.memoryUsage() + m_skyLight.memoryUsage();
    }

private:
    Palette m_storage;
    LightArray m_skyLight;
};

} // namespace mc
