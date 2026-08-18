#pragma once

#include "core/Types.hpp"

#include <optional>

namespace mc::rhi {

/// Decides where this frame's writes go in a buffer the GPU may still be reading.
///
/// **Arithmetic only, and separated from `FrameRing` on purpose.** Everything that
/// can be wrong about a ring is in the offsets -- a slot that overlaps the one the
/// GPU is reading, a sub-range that runs past the end of its slot, an offset the
/// driver rejects because it is not aligned -- and none of that needs a GL context
/// to be checked. `FrameRing` is then the memcpy and the bind, which are the two
/// parts a test could not reach anyway. See tests/test_ring_layout.cpp.
class RingLayout {
public:
    /// Three frames, for the reason `SectionMeshStore::kReuseDelayFrames` is three:
    /// the GPU is at most two frames behind with a triple-buffered swapchain, and
    /// the third is slack for a driver that queues one more.
    ///
    /// The two constants mean the same thing and are deliberately not shared. The
    /// mesh arena defers *reuse* by a frame count; this cycles *slots* by one. If a
    /// reason is ever found to change one, it will not automatically be a reason to
    /// change the other.
    static constexpr u32 kFrames = 3;

    /// `alignment` is the driver's indexed-bind granularity
    /// (`GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT`), passed in rather than queried
    /// so this class stays free of GL. Must be a power of two.
    ///
    /// `bytesPerFrame` is rounded up to `alignment`, so every slot begins at an
    /// offset the driver will accept and not only the first one.
    RingLayout(usize bytesPerFrame, usize alignment, u32 frames = kFrames);

    /// Moves to the next slot and forgets the previous frame's sub-allocations.
    /// Call once per frame, before anything reserves.
    void beginFrame();

    /// Reserves `bytes` in the current slot and returns their absolute offset in
    /// the buffer, or nothing if the slot is full.
    ///
    /// **Nothing rather than an assertion, because the caller can do something
    /// useful with it.** A frame that cannot fit its HUD should skip the HUD and
    /// say so; aborting is a worse answer to a hotbar that grew one quad past the
    /// budget than a missing hotbar is.
    std::optional<usize> reserve(usize bytes);

    usize bytesPerFrame() const noexcept { return m_bytesPerFrame; }
    usize totalBytes() const noexcept { return m_bytesPerFrame * m_frames; }
    usize alignment() const noexcept { return m_alignment; }
    u32 frames() const noexcept { return m_frames; }

    /// Bytes handed out in the current slot, including alignment padding.
    usize usedThisFrame() const noexcept { return m_used; }

    /// The largest any single frame has used. This is the number that says whether
    /// `bytesPerFrame` was estimated well, and the engine logs it.
    usize highWaterBytes() const noexcept { return m_highWater; }

    /// How many reservations have been refused for want of room, over the ring's
    /// whole life. Non-zero means something was not drawn.
    u64 refusedCount() const noexcept { return m_refused; }

    /// Which slot the current frame is writing into. Exposed for tests and for the
    /// log line that reports the ring's shape at startup.
    u32 slot() const noexcept { return m_slot; }

private:
    usize m_bytesPerFrame;
    usize m_alignment;
    u32 m_frames;

    u32 m_slot = 0;
    usize m_used = 0;
    usize m_highWater = 0;
    u64 m_refused = 0;
};

} // namespace mc::rhi
