#pragma once

#include "core/Types.hpp"
#include "rhi/Buffer.hpp"
#include "rhi/RingLayout.hpp"

#include <optional>
#include <span>

namespace mc::rhi {

/// One persistently mapped buffer that every per-frame write goes through.
///
/// **This exists because a coherent mapping says nothing about what the GPU is
/// reading.** `Buffer::createPersistent` gives a pointer that can be written from
/// any thread with no GL call, and `barrierAfterClientWrites` orders those writes
/// against the draws that follow -- but neither of them waits for *last* frame's
/// draw to finish. Writing offset 0 of the same buffer every frame therefore
/// overwrites data a queued frame may still be reading, and the only reason it is
/// not seen is that vsync leaves enough slack. That was true in five places across
/// four renderers before this class existed.
///
/// The fix is the cheap form of the triple buffering DESIGN.md 3.8 calls for: a
/// buffer holding `RingLayout::kFrames` frames' worth of room, cycling one slot per
/// frame, so a frame never writes into the slot the GPU is reading. Sub-allocation
/// within the slot is a bump pointer, which is also what makes two writes in one
/// frame -- the character and its view model, the opaque origins and the water ones
/// -- safe from each other.
///
/// **One ring rather than one per renderer, deliberately.** Five rings would be four
/// more chances to get the discipline wrong, and Phase 5's indirect command buffer
/// makes it six. It also means the budget is one number that can be reported.
///
/// Not thread-safe, and not meant to be: this is the render thread's scratch space.
/// The streaming path's arena is `SectionMeshStore`, which has its own discipline
/// because its ranges outlive the frame that wrote them.
class FrameRing {
public:
    /// A reserved sub-range of the current frame's slot. Offsets are absolute in
    /// the buffer, so they can go straight to a bind.
    struct Slice {
        usize offset = 0;
        usize size = 0;
    };

    /// `bytesPerFrame` is the budget for one frame's writes across every renderer.
    /// It is rounded up to the driver's bind alignment and multiplied by the frame
    /// count, so the allocation is a little larger than asked for.
    explicit FrameRing(usize bytesPerFrame);

    /// Cycles to the next slot. Call once per frame, before any renderer writes,
    /// and never between a write and the draw that reads it.
    void beginFrame() { m_layout.beginFrame(); }

    /// Reserves room without writing, for a caller that fills it in more than one
    /// piece. Returns nothing when the frame's budget is exhausted.
    std::optional<Slice> reserve(usize bytes);

    /// Copies into a slice at `offsetInSlice` bytes from its start.
    void write(const Slice& slice, usize offsetInSlice, std::span<const std::byte> data) const;

    /// Reserve and write in one, which is what four of the five call sites want.
    std::optional<Slice> upload(std::span<const std::byte> data);

    /// Binds the slice to an indexed target, so the shader sees the slice's start
    /// as element zero and nothing in the rest of the ring.
    void bind(BufferTarget target, u32 index, const Slice& slice) const;

    const RingLayout& layout() const noexcept { return m_layout; }
    const Buffer& buffer() const noexcept { return m_buffer; }

private:
    /// Declared before the buffer, because the buffer's size comes from it.
    RingLayout m_layout;
    Buffer m_buffer;
};

} // namespace mc::rhi
