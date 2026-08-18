#include <doctest/doctest.h>

#include "rhi/RingLayout.hpp"

#include <set>
#include <vector>

using namespace mc;
using mc::rhi::RingLayout;

// The whole point of this file is that it needs no GL context: the ring's offsets
// are arithmetic, and the arithmetic is where a frame can end up writing over the
// range the GPU is reading. The memcpy and the bind are in FrameRing, and neither
// can be checked without a device.

namespace {

/// The bind alignment a driver actually reports, so the padding cases are the ones
/// that will really happen. 256 is the harshest value in common use.
constexpr usize kAlign = 256;

} // namespace

TEST_CASE("a frame's slot is rounded up to the bind alignment") {
    // Asked for 100 bytes, which no driver would accept as a second slot's start.
    const RingLayout layout{100, kAlign};

    CHECK(layout.bytesPerFrame() == kAlign);
    CHECK(layout.totalBytes() == kAlign * RingLayout::kFrames);
    CHECK(layout.frames() == RingLayout::kFrames);
}

TEST_CASE("every offset a reservation returns is aligned") {
    RingLayout layout{4096, kAlign};

    for (u32 frame = 0; frame < 2 * RingLayout::kFrames; ++frame) {
        layout.beginFrame();
        for (usize size : {1u, 17u, 255u, 256u, 257u}) {
            const auto offset = layout.reserve(size);
            REQUIRE(offset.has_value());
            CHECK(*offset % kAlign == 0);
        }
    }
}

TEST_CASE("consecutive frames do not overlap") {
    // One reservation per frame, filling the slot, so any overlap is exact and
    // obvious rather than a partial one that a spot check could miss.
    RingLayout layout{kAlign, kAlign};

    std::vector<usize> offsets;
    for (u32 frame = 0; frame < RingLayout::kFrames; ++frame) {
        layout.beginFrame();
        const auto offset = layout.reserve(kAlign);
        REQUIRE(offset.has_value());
        offsets.push_back(*offset);
    }

    const std::set<usize> distinct{offsets.begin(), offsets.end()};
    CHECK(distinct.size() == RingLayout::kFrames);

    for (usize offset : offsets) {
        CHECK(offset + kAlign <= layout.totalBytes());
    }
}

TEST_CASE("a slot is reused only after every other slot has had a turn") {
    // This is the property the whole class exists for: with kFrames slots, the
    // offset a frame writes must not come back until kFrames frames have passed,
    // because that is how long the GPU may still be reading it.
    RingLayout layout{kAlign, kAlign};

    layout.beginFrame();
    const auto first = layout.reserve(kAlign);
    REQUIRE(first.has_value());

    for (u32 frame = 1; frame < RingLayout::kFrames; ++frame) {
        layout.beginFrame();
        const auto offset = layout.reserve(kAlign);
        REQUIRE(offset.has_value());
        CHECK(*offset != *first);
    }

    layout.beginFrame();
    const auto wrapped = layout.reserve(kAlign);
    REQUIRE(wrapped.has_value());
    CHECK(*wrapped == *first);
}

TEST_CASE("two reservations in one frame are disjoint") {
    // The character and its view model, and the opaque origins and the water ones:
    // both write twice in a frame, and sharing one buffer at offset 0 is what they
    // used to do.
    RingLayout layout{4096, kAlign};
    layout.beginFrame();

    const auto first = layout.reserve(300);
    const auto second = layout.reserve(300);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    CHECK(*first + 300 <= *second);
}

TEST_CASE("a reservation past the end of the slot is refused, not wrapped") {
    // Wrapping into the next slot would silently hand out the range the GPU is
    // reading, which is the exact bug this class replaces. Refusing loses a draw.
    RingLayout layout{1024, kAlign};
    layout.beginFrame();

    REQUIRE(layout.reserve(1000).has_value());

    CHECK_FALSE(layout.reserve(100).has_value());
    CHECK(layout.refusedCount() == 1);

    // **And nothing fits after it, however small.** 1,000 bytes leaves 24 in the
    // slot, but the next offset has to be aligned and the alignment lands exactly
    // on the end -- so the tail of a slot is unusable whenever the previous
    // reservation ended inside the last aligned block. That is the cost of an
    // aligned bump allocator, and the budget is sized with room for it rather than
    // the allocator being made cleverer.
    CHECK_FALSE(layout.reserve(1).has_value());
}

TEST_CASE("a refusal consumes nothing, so a later frame is unaffected") {
    RingLayout layout{1024, kAlign};
    layout.beginFrame();

    REQUIRE(layout.reserve(256).has_value());
    const usize usedBefore = layout.usedThisFrame();

    CHECK_FALSE(layout.reserve(4096).has_value());
    CHECK(layout.usedThisFrame() == usedBefore);

    // The frame that was refused still has all the room it had.
    CHECK(layout.reserve(768).has_value());
}

TEST_CASE("a reservation larger than a whole frame is refused") {
    RingLayout layout{1024, kAlign};
    layout.beginFrame();

    CHECK_FALSE(layout.reserve(2048).has_value());
    CHECK(layout.refusedCount() == 1);
}

TEST_CASE("a size near the limit of usize cannot wrap the bounds check") {
    // `bytes` comes from a container's size at every call site, so this is a bug
    // elsewhere rather than a real input -- but it would be a bug that returned a
    // valid-looking offset, which is worse than one that returns nothing.
    RingLayout layout{1024, kAlign};
    layout.beginFrame();

    CHECK_FALSE(layout.reserve(~usize{0}).has_value());
    CHECK_FALSE(layout.reserve(~usize{0} - 512).has_value());
}

TEST_CASE("beginFrame forgets the previous frame's use") {
    RingLayout layout{1024, kAlign};

    layout.beginFrame();
    REQUIRE(layout.reserve(1024).has_value());
    CHECK(layout.usedThisFrame() == 1024);

    layout.beginFrame();
    CHECK(layout.usedThisFrame() == 0);
    CHECK(layout.reserve(1024).has_value());
}

TEST_CASE("the high-water mark is the largest single frame, not the total") {
    RingLayout layout{4096, kAlign};

    layout.beginFrame();
    REQUIRE(layout.reserve(256).has_value());
    REQUIRE(layout.reserve(256).has_value());
    CHECK(layout.highWaterBytes() == 512);

    layout.beginFrame();
    REQUIRE(layout.reserve(256).has_value());
    CHECK(layout.highWaterBytes() == 512);

    layout.beginFrame();
    REQUIRE(layout.reserve(1024).has_value());
    CHECK(layout.highWaterBytes() == 1024);
}

TEST_CASE("a zero-byte reservation costs nothing and stays in bounds") {
    RingLayout layout{kAlign, kAlign};
    layout.beginFrame();

    const auto offset = layout.reserve(0);
    REQUIRE(offset.has_value());
    CHECK(*offset < layout.totalBytes());
    CHECK(layout.usedThisFrame() == 0);
}

TEST_CASE("a single-frame ring is legal and reuses its one slot") {
    // Not what the engine runs, but the degenerate case should behave rather than
    // divide by zero somewhere.
    RingLayout layout{kAlign, kAlign, 1};

    layout.beginFrame();
    const auto first = layout.reserve(16);
    layout.beginFrame();
    const auto second = layout.reserve(16);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
}
