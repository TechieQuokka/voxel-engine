#pragma once

#include "core/Types.hpp"

#include <span>

namespace mc::rhi {

enum class BufferTarget : u32 {
    Storage = 0, ///< SSBO -- quad data, draw commands, culling results
    Uniform = 1,
};

/// A GPU buffer created with immutable storage (glNamedBufferStorage).
///
/// Phase 1 uses static uploads. Persistent mapping and triple buffering arrive
/// in Phase 3 when chunk streaming starts writing every frame; the interface is
/// shaped to absorb that without callers changing.
class Buffer {
public:
    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    /// Allocates immutable storage and fills it once.
    static Buffer createStatic(std::span<const std::byte> data);

    /// Binds to an indexed target, e.g. `layout(binding = N)` in GLSL.
    void bindBase(BufferTarget target, u32 index) const;

    u32 handle() const noexcept { return m_handle; }
    usize size() const noexcept { return m_size; }
    bool valid() const noexcept { return m_handle != 0; }

private:
    Buffer(u32 handle, usize size) : m_handle(handle), m_size(size) {}

    u32 m_handle = 0;
    usize m_size = 0;
};

} // namespace mc::rhi
