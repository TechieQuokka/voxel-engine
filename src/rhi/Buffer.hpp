#pragma once

#include "core/Types.hpp"

#include <span>

namespace mc::rhi {

enum class BufferTarget : u32 {
    Storage = 0, ///< SSBO -- quad data, draw commands, culling results
    Uniform = 1,
};

/// A GPU buffer.
///
/// Two shapes, because the engine needs exactly two. `createStatic` allocates
/// immutable storage and fills it once, for anything that never changes.
/// `createPersistent` allocates write-mappable storage and keeps the mapping for
/// the buffer's whole life, for the streaming arena.
///
/// **Persistent mapping is what makes an upload thread possible without a second
/// GL context.** Once mapped coherently, writing to the buffer is a plain memcpy
/// into process memory and issues no GL command at all, so any thread may do it.
/// The GL side of the contract is one `barrierAfterClientWrites()` on the thread
/// that owns the context, before the draw that reads the data.
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

    /// Allocates `size` bytes of persistently mapped, coherent, write-only storage.
    ///
    /// Coherent rather than explicitly flushed: the alternative needs
    /// glFlushMappedNamedBufferRange, which is a GL call and would drag a context
    /// back onto the upload thread -- defeating the point.
    static Buffer createPersistent(usize size);

    /// Copies into the mapped range. Thread-safe with respect to other writes to
    /// disjoint ranges, which is the whole access pattern of the arena.
    ///
    /// The caller is responsible for not overwriting a range the GPU may still be
    /// reading; see SectionMeshStore, which defers reuse by a few frames.
    void write(usize offset, std::span<const std::byte> data) const;

    /// Makes client writes to persistently mapped buffers visible to the GPU.
    ///
    /// Required by the spec even for coherent mappings: coherence removes the need
    /// to flush, not the need to order. Must be called on the context-owning
    /// thread, once per frame, before drawing.
    static void barrierAfterClientWrites();

    /// Binds to an indexed target, e.g. `layout(binding = N)` in GLSL.
    void bindBase(BufferTarget target, u32 index) const;

    u32 handle() const noexcept { return m_handle; }
    usize size() const noexcept { return m_size; }
    bool valid() const noexcept { return m_handle != 0; }
    bool mapped() const noexcept { return m_mapped != nullptr; }

private:
    Buffer(u32 handle, usize size, void* mapped)
        : m_handle(handle), m_size(size), m_mapped(mapped) {}

    u32 m_handle = 0;
    usize m_size = 0;
    /// Non-null only for persistent buffers. Unmapping is implicit in deletion.
    void* m_mapped = nullptr;
};

} // namespace mc::rhi
