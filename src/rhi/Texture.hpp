#pragma once

#include "core/Types.hpp"

#include <span>

namespace mc::rhi {

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
    static TextureArray create(u32 size, u32 layerCount, std::span<const u8> pixels);

    void bind(u32 unit) const;

    u32 handle() const noexcept { return m_handle; }
    u32 layerCount() const noexcept { return m_layerCount; }

private:
    TextureArray(u32 handle, u32 layerCount) : m_handle(handle), m_layerCount(layerCount) {}

    u32 m_handle = 0;
    u32 m_layerCount = 0;
};

} // namespace mc::rhi
