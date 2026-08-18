#include "rhi/FrameRing.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"

namespace mc::rhi {

FrameRing::FrameRing(usize bytesPerFrame)
    : m_layout(bytesPerFrame, Buffer::storageOffsetAlignment()),
      m_buffer(Buffer::createPersistent(m_layout.totalBytes())) {
    logInfo("Frame ring: {} KiB per frame x {} frames ({} KiB), {}-byte bind alignment",
            m_layout.bytesPerFrame() / 1024, m_layout.frames(), m_layout.totalBytes() / 1024,
            m_layout.alignment());
}

std::optional<FrameRing::Slice> FrameRing::reserve(usize bytes) {
    const std::optional<usize> offset = m_layout.reserve(bytes);
    if (!offset.has_value()) {
        // Warned once per frame at most in practice, because a frame that overflows
        // usually overflows on its largest writer. Loud on purpose: something did
        // not get drawn, and a silently missing HUD would be blamed on the HUD.
        logWarn("Frame ring full: {} bytes refused with {} of {} used. "
                "Raise the per-frame budget.",
                bytes, m_layout.usedThisFrame(), m_layout.bytesPerFrame());
        return std::nullopt;
    }

    return Slice{*offset, bytes};
}

void FrameRing::write(const Slice& slice, usize offsetInSlice,
                      std::span<const std::byte> data) const {
    MC_ASSERT(offsetInSlice + data.size_bytes() <= slice.size);
    m_buffer.write(slice.offset + offsetInSlice, data);
}

std::optional<FrameRing::Slice> FrameRing::upload(std::span<const std::byte> data) {
    const std::optional<Slice> slice = reserve(data.size_bytes());
    if (!slice.has_value()) {
        return std::nullopt;
    }

    write(*slice, 0, data);
    return slice;
}

void FrameRing::bind(BufferTarget target, u32 index, const Slice& slice) const {
    // GL rejects a zero-sized range, and every caller already returns early when it
    // has nothing to draw -- so reaching here with an empty slice is a caller bug
    // rather than an empty frame.
    MC_ASSERT(slice.size > 0);
    m_buffer.bindRange(target, index, slice.offset, slice.size);
}

} // namespace mc::rhi
