#include "rhi/Buffer.hpp"

#include "core/Assert.hpp"

#include <glad/gl.h>

#include <cstring>
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
        // glDeleteBuffers unmaps implicitly, so there is no glUnmapNamedBuffer here.
        glDeleteBuffers(1, &m_handle);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_handle(std::exchange(other.m_handle, 0)),
      m_size(std::exchange(other.m_size, 0)),
      m_mapped(std::exchange(other.m_mapped, nullptr)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (m_handle != 0) {
            glDeleteBuffers(1, &m_handle);
        }
        m_handle = std::exchange(other.m_handle, 0);
        m_size = std::exchange(other.m_size, 0);
        m_mapped = std::exchange(other.m_mapped, nullptr);
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

    return Buffer(handle, data.size_bytes(), nullptr);
}

Buffer Buffer::createPersistent(usize size) {
    MC_VERIFY(size > 0);

    GLuint handle = 0;
    glCreateBuffers(1, &handle);
    MC_VERIFY_MSG(handle != 0, "glCreateBuffers failed");

    constexpr GLbitfield kStorageFlags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

    glNamedBufferStorage(handle, static_cast<GLsizeiptr>(size), nullptr, kStorageFlags);

    // No GL_MAP_READ_BIT: the mapping is write-only, so the driver may place it in
    // memory that reads back appallingly slowly, and nothing here reads it.
    void* mapped =
        glMapNamedBufferRange(handle, 0, static_cast<GLsizeiptr>(size), kStorageFlags);
    MC_VERIFY_MSG(mapped != nullptr, "glMapNamedBufferRange failed");

    return Buffer(handle, size, mapped);
}

void Buffer::write(usize offset, std::span<const std::byte> data) const {
    MC_VERIFY_MSG(m_mapped != nullptr, "write() on a buffer that is not persistently mapped");
    MC_VERIFY_MSG(offset + data.size_bytes() <= m_size, "write() past the end of the buffer");

    std::memcpy(static_cast<std::byte*>(m_mapped) + offset, data.data(), data.size_bytes());
}

void Buffer::barrierAfterClientWrites() {
    glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
}

void Buffer::bindBase(BufferTarget target, u32 index) const {
    glBindBufferBase(toGlTarget(target), index, m_handle);
}

} // namespace mc::rhi
