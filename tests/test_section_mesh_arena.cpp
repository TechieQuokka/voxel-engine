#include "render/SectionMeshArena.hpp"

#include <doctest/doctest.h>

#include <set>
#include <thread>
#include <vector>

using namespace mc;

namespace {

/// Quads are the arena's unit, so sizes are written in them rather than in bytes.
constexpr usize kQuad = sizeof(Quad);

usize quads(usize count) { return count * kQuad; }

SectionPos at(i32 x, i32 y, i32 z) { return SectionPos{x, y, z}; }

/// Reserves an all-opaque mesh, which is what most of these cases care about --
/// the split counts have their own case.
std::optional<usize> reserveOpaque(SectionMeshArena& arena, SectionPos pos, u32 quadCount,
                                   u64 frame) {
    return arena.reserve(pos, quadCount, quadCount, 0, 0, frame);
}

/// Advances past the reuse delay so everything released on `frame` is collectable.
void recycleAfterDelay(SectionMeshArena& arena, u64 frame) {
    arena.recycle(frame + SectionMeshArena::kReuseDelayFrames);
}

} // namespace

// -- Placement bookkeeping ------------------------------------------------------

TEST_CASE("a reserved section reports where it landed and how it splits") {
    SectionMeshArena arena(quads(64));

    const std::optional<usize> offset = arena.reserve(at(0, 0, 0), 10, 5, 2, 2, 1);
    REQUIRE(offset.has_value());

    const std::optional<SectionMeshArena::Placement> found = arena.find(at(0, 0, 0));
    REQUIRE(found.has_value());
    CHECK(found->byteOffset == *offset);
    CHECK(found->quadCount == 10);
    CHECK(found->opaqueCount == 5);
    CHECK(found->modelCount == 2);
    CHECK(found->cutoutCount == 2);
    // The last pass is whatever is left, which is the invariant ChunkRenderer's
    // four draws rest on.
    CHECK(found->translucentCount() == 1);

    CHECK(arena.sectionCount() == 1);
    CHECK(arena.usedBytes() >= quads(10));
}

TEST_CASE("a section nobody reserved has no placement") {
    SectionMeshArena arena(quads(64));
    CHECK_FALSE(arena.find(at(3, 1, 4)).has_value());
    CHECK(arena.sectionCount() == 0);
    CHECK(arena.usedBytes() == 0);
}

TEST_CASE("every reserved range begins on a whole quad") {
    // The shader indexes the arena as an array of quads from byte zero, so an
    // offset that is not a multiple of one draws every section after it shifted.
    SectionMeshArena arena(quads(256));

    for (i32 i = 0; i < 12; ++i) {
        // Deliberately ragged sizes, so a run of them would drift off alignment
        // if the allocator were not asked to keep it.
        const auto count = static_cast<u32>(1 + (i * 7) % 13);
        const std::optional<usize> offset = reserveOpaque(arena, at(i, 0, 0), count, 1);
        REQUIRE(offset.has_value());
        CHECK(*offset % kQuad == 0);
    }
}

// -- Deferred reuse -------------------------------------------------------------

TEST_CASE("a released range is not reusable until the delay has passed") {
    // **The whole reason this class defers anything.** A frame the GPU has not
    // finished may still be reading the range, and a coherent mapping does not
    // protect against that. Handing the bytes straight back is the bug this
    // guards, and it would show as flickering geometry rather than a crash.
    SectionMeshArena arena(quads(8));

    REQUIRE(reserveOpaque(arena, at(0, 0, 0), 8, 100).has_value());
    CHECK(arena.usedBytes() == quads(8));

    arena.release(at(0, 0, 0), 100);

    // Gone from the map immediately -- it must stop being drawn this frame.
    CHECK_FALSE(arena.find(at(0, 0, 0)).has_value());
    CHECK(arena.sectionCount() == 0);
    // But still spoken for, so nothing else can be handed those bytes.
    CHECK(arena.pendingReuseBytes() == quads(8));
    CHECK(arena.usedBytes() == quads(8));

    for (u64 frame = 100; frame < 100 + SectionMeshArena::kReuseDelayFrames; ++frame) {
        arena.recycle(frame);
        CHECK(arena.pendingReuseBytes() == quads(8));
        CHECK_FALSE(reserveOpaque(arena, at(1, 0, 0), 8, frame).has_value());
    }

    arena.recycle(100 + SectionMeshArena::kReuseDelayFrames);
    CHECK(arena.pendingReuseBytes() == 0);
    CHECK(arena.usedBytes() == 0);
    CHECK(reserveOpaque(arena, at(1, 0, 0), 8, 200).has_value());
}

TEST_CASE("replacing a section's mesh retires the old range rather than overwriting") {
    // An updated mesh always takes a fresh range; the old one goes on the pending
    // list. Writing in place would be the same hazard as immediate reuse.
    SectionMeshArena arena(quads(32));

    const std::optional<usize> first = reserveOpaque(arena, at(0, 0, 0), 4, 10);
    REQUIRE(first.has_value());

    const std::optional<usize> second = reserveOpaque(arena, at(0, 0, 0), 4, 10);
    REQUIRE(second.has_value());
    CHECK(*second != *first);

    // One section, two ranges: the live one and the retired one.
    CHECK(arena.sectionCount() == 1);
    CHECK(arena.pendingReuseBytes() == quads(4));
    CHECK(arena.find(at(0, 0, 0))->byteOffset == *second);

    recycleAfterDelay(arena, 10);
    CHECK(arena.pendingReuseBytes() == 0);
    CHECK(arena.usedBytes() == quads(4)); // Only the live range is still held.
}

TEST_CASE("recycle keeps young entries while collecting old ones") {
    // The pending list is compacted in place rather than erased from the middle,
    // so a mixed-age list is exactly where an off-by-one would show.
    SectionMeshArena arena(quads(64));

    REQUIRE(reserveOpaque(arena, at(0, 0, 0), 4, 10).has_value());
    REQUIRE(reserveOpaque(arena, at(1, 0, 0), 4, 10).has_value());
    REQUIRE(reserveOpaque(arena, at(2, 0, 0), 4, 10).has_value());

    arena.release(at(0, 0, 0), 10);
    arena.release(at(1, 0, 0), 20);
    arena.release(at(2, 0, 0), 30);
    CHECK(arena.pendingReuseBytes() == quads(12));

    // Old enough to collect the first only.
    arena.recycle(10 + SectionMeshArena::kReuseDelayFrames);
    CHECK(arena.pendingReuseBytes() == quads(8));

    arena.recycle(20 + SectionMeshArena::kReuseDelayFrames);
    CHECK(arena.pendingReuseBytes() == quads(4));

    arena.recycle(30 + SectionMeshArena::kReuseDelayFrames);
    CHECK(arena.pendingReuseBytes() == 0);
    CHECK(arena.usedBytes() == 0);
}

TEST_CASE("releasing a section that has no storage does nothing") {
    SectionMeshArena arena(quads(16));
    arena.release(at(9, 9, 9), 1);
    CHECK(arena.pendingReuseBytes() == 0);
    CHECK(arena.sectionCount() == 0);
}

// -- Arena exhaustion -----------------------------------------------------------

TEST_CASE("a full arena refuses a reservation instead of failing") {
    // Refusing is a normal outcome, not an error: the caller marks the section
    // dirty again and tries next frame. The engine counts these as arenaFullEvents.
    SectionMeshArena arena(quads(16));

    REQUIRE(reserveOpaque(arena, at(0, 0, 0), 16, 1).has_value());
    CHECK(arena.usedBytes() == quads(16));

    CHECK_FALSE(reserveOpaque(arena, at(1, 0, 0), 1, 1).has_value());
    CHECK_FALSE(arena.find(at(1, 0, 0)).has_value());
    // The refusal must not have disturbed anything already placed.
    CHECK(arena.sectionCount() == 1);
    CHECK(arena.find(at(0, 0, 0)).has_value());
}

TEST_CASE("a refused update leaves the section drawing the mesh it already had") {
    // **The sharpest case in this file.** If a growing mesh could not be allocated
    // and the old placement had already been retired, the section would vanish
    // until the arena drained -- a hole in the world rather than a stale mesh.
    SectionMeshArena arena(quads(16));

    const std::optional<usize> original = reserveOpaque(arena, at(0, 0, 0), 10, 1);
    REQUIRE(original.has_value());
    // Fill the rest so a larger mesh for the same section cannot be placed.
    REQUIRE(reserveOpaque(arena, at(1, 0, 0), 6, 1).has_value());

    CHECK_FALSE(reserveOpaque(arena, at(0, 0, 0), 12, 2).has_value());

    const std::optional<SectionMeshArena::Placement> still = arena.find(at(0, 0, 0));
    REQUIRE(still.has_value());
    CHECK(still->byteOffset == *original);
    CHECK(still->quadCount == 10);
    CHECK(arena.pendingReuseBytes() == 0); // Nothing was retired on the way out.
}

TEST_CASE("an arena recovers its whole capacity once everything is recycled") {
    // Fragmentation that never heals is how a long session ends up refusing meshes
    // with most of the arena nominally free.
    SectionMeshArena arena(quads(64));

    for (u64 round = 0; round < 8; ++round) {
        for (i32 i = 0; i < 8; ++i) {
            REQUIRE(reserveOpaque(arena, at(i, 0, 0), 8, round).has_value());
        }
        CHECK(arena.usedBytes() == quads(64));

        for (i32 i = 0; i < 8; ++i) {
            arena.release(at(i, 0, 0), round);
        }
        recycleAfterDelay(arena, round);

        CHECK(arena.usedBytes() == 0);
        CHECK(arena.pendingReuseBytes() == 0);
        CHECK(arena.largestFreeBlock() == quads(64));
    }
}

TEST_CASE("a reservation the arena could never satisfy is refused cleanly") {
    SectionMeshArena arena(quads(8));
    CHECK_FALSE(reserveOpaque(arena, at(0, 0, 0), 9, 1).has_value());
    CHECK(arena.sectionCount() == 0);
    CHECK(arena.usedBytes() == 0);
}

// -- Concurrency ----------------------------------------------------------------

TEST_CASE("concurrent reservations hand out ranges that do not overlap") {
    // The upload thread reserves while the main thread recycles and reads stats.
    // Two sections sharing bytes is the failure this rules out, and it is one that
    // would show on screen as one chunk wearing another's geometry.
    constexpr usize kThreads = 4;
    constexpr u32 kPerThread = 50;
    constexpr u32 kQuadsEach = 4;

    SectionMeshArena arena(quads(kThreads * kPerThread * kQuadsEach));

    std::vector<std::thread> threads;
    std::vector<std::vector<usize>> offsets(kThreads);

    for (usize t = 0; t < kThreads; ++t) {
        threads.emplace_back([&arena, &offsets, t]() {
            for (u32 i = 0; i < kPerThread; ++i) {
                const auto pos = at(static_cast<i32>(t), 0, static_cast<i32>(i));
                const std::optional<usize> offset = reserveOpaque(arena, pos, kQuadsEach, 1);
                if (offset.has_value()) {
                    offsets[t].push_back(*offset);
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    std::set<usize> seen;
    usize total = 0;
    for (const std::vector<usize>& perThread : offsets) {
        for (const usize offset : perThread) {
            CHECK(seen.insert(offset).second); // Never handed out twice.
            ++total;
        }
    }

    CHECK(total == kThreads * kPerThread);
    CHECK(arena.sectionCount() == kThreads * kPerThread);
    CHECK(arena.usedBytes() == quads(total * kQuadsEach));
}
