#pragma once

#include "core/Types.hpp"
#include "rhi/Texture.hpp"

#include <span>
#include <vector>

namespace mc {

/// Builds the block texture array.
///
/// Textures are generated procedurally rather than loaded from image files.
/// For this project that is the better default: it keeps binary assets out of
/// the repository, the result is deterministic, and it removes an image-loading
/// dependency entirely. Replacing this with authored PNGs later only changes
/// how the pixel buffer is filled -- the array, the layer indices, and the
/// shader all stay as they are.
class BlockTextures {
public:
    static constexpr u32 kTextureSize = 16;

    BlockTextures();

    void bind(u32 unit) const { m_texture.bind(unit); }

    const rhi::TextureArray& texture() const noexcept { return m_texture; }

    /// One layer's RGBA pixels, row-major, **top row first** -- the order the
    /// generators write them and the order `hud.vert` samples them in.
    ///
    /// **Kept on the CPU because a held item is geometry, not a picture.** An item
    /// in the hand is an extruded sprite: its silhouette becomes rim faces whose
    /// colour is the edge pixel's, which means something has to be able to read a
    /// pixel back. The whole array is 60-odd KiB, so keeping it costs less than the
    /// machinery of regenerating one layer would.
    std::span<const u8> layerPixels(u32 layer) const;

private:
    rhi::TextureArray m_texture;
    /// Every layer's pixels, in the same layout the upload used.
    std::vector<u8> m_pixels;
};

} // namespace mc
