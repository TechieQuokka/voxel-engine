#pragma once

#include "core/Types.hpp"
#include "rhi/Texture.hpp"

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

private:
    rhi::TextureArray m_texture;
};

} // namespace mc
