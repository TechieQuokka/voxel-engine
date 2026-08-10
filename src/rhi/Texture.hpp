#pragma once

#include "core/Types.hpp"

#include <span>

namespace mc::rhi {

/// Storage format of an 8-bit-per-channel texture.
///
/// The distinction is not cosmetic: with Srgb8A8 the sampler decodes to linear
/// on every fetch, so shading arithmetic happens in light-linear space. Data
/// that is not a colour -- brickmap indices, material ids, heightfields -- must
/// use Rgba8, or the decode would corrupt it.
enum class ColorSpace : u32 {
    Rgba8 = 0,   ///< Values pass through untouched.
    Srgb8A8 = 1, ///< Sampled values are decoded from sRGB to linear.
};

/// A GL_TEXTURE_2D_ARRAY of RGBA8 images, all the same size.
///
/// An array texture rather than an atlas, deliberately. An atlas needs UV
/// padding to stop neighbouring tiles bleeding under filtering and mipmapping,
/// and that padding is exactly what breaks greedy meshing: a merged 8x3 quad
/// has to tile its texture, which an atlas cannot do without clamping tricks.
/// With an array, the layer index is per-quad and wrapping just works.
class TextureArray {
public:
    TextureArray() = default;
    ~TextureArray();

    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;
    TextureArray(TextureArray&& other) noexcept;
    TextureArray& operator=(TextureArray&& other) noexcept;

    /// `pixels` holds `layerCount` images of `size` x `size` RGBA8, back to
    /// back. Mipmaps are generated automatically.
    ///
    /// `space` is required rather than defaulted: getting it wrong is invisible
    /// in the code and obvious only on screen, so the call site has to say.
    static TextureArray create(u32 size,
                               u32 layerCount,
                               std::span<const u8> pixels,
                               ColorSpace space);

    void bind(u32 unit) const;

    u32 handle() const noexcept { return m_handle; }
    u32 layerCount() const noexcept { return m_layerCount; }

private:
    TextureArray(u32 handle, u32 layerCount) : m_handle(handle), m_layerCount(layerCount) {}

    u32 m_handle = 0;
    u32 m_layerCount = 0;
};

} // namespace mc::rhi
