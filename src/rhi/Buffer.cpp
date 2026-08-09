#include "rhi/Buffer.hpp"

#include "core/Assert.hpp"

#include <glad/gl.h>

#include <utility>

namespace mc::rhi {
namespace {

GLenum toGlTarget(BufferTarget target) {
    switch (target) {
    case BufferTarget::Storage: return GL_SHADER_STORAGE_BUFFER;
    case BufferTarget::Uniform: return GL_UNIFORM_BUFFER;
    }
    return GL_SHADER_STORAGE_BUFFER;
}

} // namespace

Buffer::~Buffer() {
    if (m_handle != 0) {
        glDeleteBuffers(1, &m_handle);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_handle(std::exchange(other.m_handle, 0)), m_size(std::exchange(other.m_size, 0)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (m_handle != 0) {
            glDeleteBuffers(1, &m_handle);
        }
        m_handle = std::exchange(other.m_handle, 0);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

Buffer Buffer::createStatic(std::span<const std::byte> data) {
    GLuint handle = 0;
    glCreateBuffers(1, &handle);
    MC_VERIFY_MSG(handle != 0, "glCreateBuffers failed");

    // No storage flags: the contents never change after creation, which lets
    // the driver place it in the most favourable memory it has.
    glNamedBufferStorage(handle, static_cast<GLsizeiptr>(data.size_bytes()), data.data(), 0);

    return Buffer(handle, data.size_bytes());
}

void Buffer::bindBase(BufferTarget target, u32 index) const {
    glBindBufferBase(toGlTarget(target), index, m_handle);
}

} // namespace mc::rhi
