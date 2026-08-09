#include "rhi/VertexArray.hpp"

#include "core/Assert.hpp"

#include <glad/gl.h>

#include <utility>

namespace mc::rhi {

VertexArray::VertexArray() {
    glCreateVertexArrays(1, &m_handle);
    MC_VERIFY_MSG(m_handle != 0, "glCreateVertexArrays failed");
}

VertexArray::~VertexArray() {
    if (m_handle != 0) {
        glDeleteVertexArrays(1, &m_handle);
    }
}

VertexArray::VertexArray(VertexArray&& other) noexcept
    : m_handle(std::exchange(other.m_handle, 0)) {}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        if (m_handle != 0) {
            glDeleteVertexArrays(1, &m_handle);
        }
        m_handle = std::exchange(other.m_handle, 0);
    }
    return *this;
}

void VertexArray::bind() const {
    glBindVertexArray(m_handle);
}

} // namespace mc::rhi
