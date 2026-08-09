#include "rhi/Texture.hpp"

#include "core/Assert.hpp"

#include <glad/gl.h>

#include <bit>
#include <utility>

namespace mc::rhi {

TextureArray::~TextureArray() {
    if (m_handle != 0) {
        glDeleteTextures(1, &m_handle);
    }
}

TextureArray::TextureArray(TextureArray&& other) noexcept
    : m_handle(std::exchange(other.m_handle, 0)),
      m_layerCount(std::exchange(other.m_layerCount, 0)) {}

TextureArray& TextureArray::operator=(TextureArray&& other) noexcept {
    if (this != &other) {
        if (m_handle != 0) {
            glDeleteTextures(1, &m_handle);
        }
        m_handle = std::exchange(other.m_handle, 0);
        m_layerCount = std::exchange(other.m_layerCount, 0);
    }
    return *this;
}

TextureArray TextureArray::create(u32 size, u32 layerCount, std::span<const u8> pixels) {
    MC_VERIFY(size > 0 && layerCount > 0);
    MC_VERIFY(std::has_single_bit(size));
    MC_VERIFY(pixels.size() == static_cast<usize>(size) * size * layerCount * 4);

    GLuint handle = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &handle);
    MC_VERIFY_MSG(handle != 0, "glCreateTextures failed");

    const auto levels = static_cast<GLsizei>(std::bit_width(size));
    glTextureStorage3D(handle, levels, GL_RGBA8,
                       static_cast<GLsizei>(size),
                       static_cast<GLsizei>(size),
                       static_cast<GLsizei>(layerCount));

    glTextureSubImage3D(handle, 0, 0, 0, 0,
                        static_cast<GLsizei>(size),
                        static_cast<GLsizei>(size),
                        static_cast<GLsizei>(layerCount),
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    glGenerateTextureMipmap(handle);

    // Nearest magnification keeps voxel texels crisp instead of smearing them.
    // Minification is mipmapped and linear between levels, otherwise distant
    // terrain aliases badly -- which matters more here than in most engines,
    // since the whole project targets a very long view distance.
    glTextureParameteri(handle, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(handle, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // REPEAT is what lets a merged quad tile its texture across the whole
    // merged area. An atlas could not do this.
    glTextureParameteri(handle, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(handle, GL_TEXTURE_WRAP_T, GL_REPEAT);

    GLfloat maxAnisotropy = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy);
    glTextureParameterf(handle, GL_TEXTURE_MAX_ANISOTROPY, maxAnisotropy);

    return TextureArray(handle, layerCount);
}

void TextureArray::bind(u32 unit) const {
    glBindTextureUnit(unit, m_handle);
}

} // namespace mc::rhi
