#include "rhi/RingLayout.hpp"

#include "core/Assert.hpp"

namespace mc::rhi {
namespace {

usize alignUp(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

RingLayout::RingLayout(usize bytesPerFrame, usize alignment, u32 frames)
    : m_bytesPerFrame(0), m_alignment(alignment), m_frames(frames) {
    MC_VERIFY_MSG(alignment > 0 && (alignment & (alignment - 1)) == 0,
                  "ring alignment must be a power of two");
    MC_VERIFY_MSG(frames > 0, "a ring needs at least one frame");
    MC_VERIFY_MSG(bytesPerFrame > 0, "a ring needs a non-empty frame");

    // Rounded up here rather than at every reserve: it makes each slot's *start*
    // aligned, which is what lets an aligned offset within a slot be an aligned
    // offset in the buffer.
    m_bytesPerFrame = alignUp(bytesPerFrame, alignment);
}

void RingLayout::beginFrame() {
    m_slot = (m_slot + 1) % m_frames;
    m_used = 0;
}

std::optional<usize> RingLayout::reserve(usize bytes) {
    const usize start = alignUp(m_used, m_alignment);

    // Two conditions, not one: the addition itself must not wrap. `bytes` comes from
    // a container's size in every caller, so a nonsense value here would be a bug
    // elsewhere -- but it would be a bug that returned a valid-looking offset.
    if (start > m_bytesPerFrame || bytes > m_bytesPerFrame - start) {
        ++m_refused;
        return std::nullopt;
    }

    m_used = start + bytes;
    if (m_used > m_highWater) {
        m_highWater = m_used;
    }

    return static_cast<usize>(m_slot) * m_bytesPerFrame + start;
}

} // namespace mc::rhi
