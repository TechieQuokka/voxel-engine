#pragma once

#include "core/Types.hpp"

namespace mc::rhi {

/// A vertex array object.
///
/// This engine does not use vertex buffers -- geometry is pulled from SSBOs and
/// expanded from gl_VertexID (DESIGN.md 3.7). A core-profile context still
/// requires *some* VAO to be bound for any draw, so this exists to satisfy the
/// specification, not to describe vertex layout.
class VertexArray {
public:
    VertexArray();
    ~VertexArray();

    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;

    void bind() const;

    u32 handle() const noexcept { return m_handle; }

private:
    u32 m_handle = 0;
};

} // namespace mc::rhi
