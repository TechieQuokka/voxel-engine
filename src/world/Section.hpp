#pragma once

#include "world/Coords.hpp"
#include "world/LightArray.hpp"
#include "world/Palette.hpp"

#include <algorithm>

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

    /// Sky light, 0 to 15. Daylight falling in from above.
    u8 skyLight(i32 x, i32 y, i32 z) const { return m_skyLight.get(x, y, z); }
    void setSkyLight(i32 x, i32 y, i32 z, u8 level) { m_skyLight.set(x, y, z, level); }

    const LightArray& skyLightArray() const noexcept { return m_skyLight; }
    LightArray& skyLightArray() noexcept { return m_skyLight; }

    /// Block light, 0 to 15. What torches emit.
    ///
    /// **A second channel rather than one combined number, and the reason is that
    /// the two are maintained by different code on different schedules.** Sky light
    /// is recomputed for a whole column from its heightmap; block light spreads
    /// incrementally in world space from wherever an emitter was placed. Storing
    /// only the combined value would mean breaking a torch could not tell how much
    /// of the brightness under it was daylight, so it could not know what to leave
    /// behind. The combination happens at mesh time and only there -- see
    /// DESIGN.md 3.7, and `BinaryGreedyMesher`, which takes `max` of the two into
    /// the sixteen light bits the quad already carries.
    ///
    /// Costs nothing until something emits: no generated block has a luminance, so
    /// this array stays uniform zero in every section of an untouched world, and a
    /// uniform `LightArray` holds no storage at all.
    u8 blockLight(i32 x, i32 y, i32 z) const { return m_blockLight.get(x, y, z); }
    void setBlockLight(i32 x, i32 y, i32 z, u8 level) { m_blockLight.set(x, y, z, level); }

    const LightArray& blockLightArray() const noexcept { return m_blockLight; }
    LightArray& blockLightArray() noexcept { return m_blockLight; }

    /// What the mesher draws with: whichever channel is brighter. DESIGN.md 3.7.
    u8 light(i32 x, i32 y, i32 z) const {
        return std::max(m_skyLight.get(x, y, z), m_blockLight.get(x, y, z));
    }

    usize memoryUsage() const noexcept {
        return m_storage.memoryUsage() + m_skyLight.memoryUsage()
               + m_blockLight.memoryUsage();
    }

private:
    Palette m_storage;
    LightArray m_skyLight;
    LightArray m_blockLight;
};

} // namespace mc
