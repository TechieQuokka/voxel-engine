#include "core/RangeAllocator.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <vector>

using namespace mc;

TEST_CASE("a fresh allocator is one free block spanning everything") {
    RangeAllocator allocator(1024);

    CHECK(allocator.capacity() == 1024);
    CHECK(allocator.used() == 0);
    CHECK(allocator.available() == 1024);
    CHECK(allocator.freeBlockCount() == 1);
    CHECK(allocator.largestFreeBlock() == 1024);
}

TEST_CASE("allocations are handed out in order and do not overlap") {
    RangeAllocator allocator(1024);

    const usize a = allocator.allocate(100);
    const usize b = allocator.allocate(200);
    const usize c = allocator.allocate(50);

    CHECK(a == 0);
    CHECK(b == 100);
    CHECK(c == 300);
    CHECK(allocator.used() == 350);
}

TEST_CASE("an allocation larger than the arena fails") {
    RangeAllocator allocator(64);

    CHECK(allocator.allocate(65) == RangeAllocator::kInvalidOffset);
    CHECK(allocator.allocate(64) == 0);
    CHECK(allocator.allocate(1) == RangeAllocator::kInvalidOffset);
    CHECK(allocator.available() == 0);
}

TEST_CASE("alignment is honoured and the padding stays allocatable") {
    RangeAllocator allocator(1024);

    REQUIRE(allocator.allocate(1) == 0); // Leaves offset 1 as the next free byte.

    const usize aligned = allocator.allocate(16, 8);
    CHECK(aligned == 8);

    // The 7 bytes of padding were returned to the free list, not consumed -- the
    // difference matters, because a mesh arena aligns every single allocation.
    CHECK(allocator.used() == 17);
    const usize padding = allocator.allocate(7);
    CHECK(padding == 1);
}

TEST_CASE("releasing coalesces with both neighbours") {
    RangeAllocator allocator(300);

    const usize a = allocator.allocate(100);
    const usize b = allocator.allocate(100);
    const usize c = allocator.allocate(100);
    REQUIRE(allocator.freeBlockCount() == 0);

    allocator.release(a, 100);
    CHECK(allocator.freeBlockCount() == 1);

    allocator.release(c, 100);
    CHECK(allocator.freeBlockCount() == 2); // Not adjacent to a yet.

    // Freeing the middle must merge all three into one, or the arena would
    // fragment until nothing large fits despite plenty being free.
    allocator.release(b, 100);
    CHECK(allocator.freeBlockCount() == 1);
    CHECK(allocator.largestFreeBlock() == 300);
    CHECK(allocator.used() == 0);
}

TEST_CASE("a released block can be reused at full size") {
    RangeAllocator allocator(1024);

    const usize first = allocator.allocate(512);
    allocator.release(first, 512);

    CHECK(allocator.allocate(1024) == 0);
}

TEST_CASE("fragmentation is visible in largestFreeBlock, not in available") {
    RangeAllocator allocator(300);

    const usize a = allocator.allocate(100);
    allocator.allocate(100); // Held, splitting the arena.
    const usize c = allocator.allocate(100);

    allocator.release(a, 100);
    allocator.release(c, 100);

    // 200 bytes free, but the largest contiguous run is 100. This is exactly the
    // signal the mesh store logs when a store() fails.
    CHECK(allocator.available() == 200);
    CHECK(allocator.largestFreeBlock() == 100);
    CHECK(allocator.allocate(150) == RangeAllocator::kInvalidOffset);
    CHECK(allocator.allocate(100) != RangeAllocator::kInvalidOffset);
}

TEST_CASE("reset returns the arena to one block") {
    RangeAllocator allocator(500);
    allocator.allocate(100);
    allocator.allocate(100);

    allocator.reset();

    CHECK(allocator.used() == 0);
    CHECK(allocator.freeBlockCount() == 1);
    CHECK(allocator.largestFreeBlock() == 500);
}

TEST_CASE("randomized churn never hands out overlapping ranges") {
    // The property that matters: two live allocations must never intersect, however
    // the free list has been carved up. A coalescing bug shows up here and is
    // painful to find any other way, because on the GPU it looks like one section
    // rendering another's geometry.
    constexpr usize kCapacity = 1u << 16;
    RangeAllocator allocator(kCapacity);

    struct Live {
        usize offset;
        usize size;
    };
    std::vector<Live> live;

    std::mt19937 rng(12345);
    std::uniform_int_distribution<usize> sizeDist(8, 2048);
    std::uniform_int_distribution<int> actionDist(0, 99);

    for (int step = 0; step < 20000; ++step) {
        const bool wantFree = !live.empty() && (actionDist(rng) < 45);

        if (wantFree) {
            std::uniform_int_distribution<usize> pick(0, live.size() - 1);
            const usize index = pick(rng);
            allocator.release(live[index].offset, live[index].size);
            live[index] = live.back();
            live.pop_back();
            continue;
        }

        const usize size = sizeDist(rng) & ~usize{7};
        const usize offset = allocator.allocate(size == 0 ? 8 : size, 8);
        if (offset == RangeAllocator::kInvalidOffset) {
            continue; // Full or too fragmented; a normal outcome.
        }

        REQUIRE(offset % 8 == 0);
        REQUIRE(offset + size <= kCapacity);

        for (const Live& other : live) {
            const bool disjoint = offset + size <= other.offset || other.offset + other.size <= offset;
            REQUIRE(disjoint);
        }
        live.push_back(Live{offset, size});
    }

    usize liveBytes = 0;
    for (const Live& entry : live) {
        liveBytes += entry.size;
    }
    CHECK(allocator.used() == liveBytes);

    for (const Live& entry : live) {
        allocator.release(entry.offset, entry.size);
    }
    CHECK(allocator.used() == 0);
    CHECK(allocator.freeBlockCount() == 1);
    CHECK(allocator.largestFreeBlock() == kCapacity);
}
