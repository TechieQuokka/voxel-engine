#include "core/MpmcQueue.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <numeric>
#include <thread>
#include <vector>

using namespace mc;

TEST_CASE("capacity rounds up to a power of two") {
    CHECK(MpmcQueue<int>(1).capacity() == 2);
    CHECK(MpmcQueue<int>(2).capacity() == 2);
    CHECK(MpmcQueue<int>(3).capacity() == 4);
    CHECK(MpmcQueue<int>(100).capacity() == 128);
    CHECK(MpmcQueue<int>(1024).capacity() == 1024);
}

TEST_CASE("popping an empty queue fails and leaves the output alone") {
    MpmcQueue<int> queue(4);

    int value = 1234;
    CHECK_FALSE(queue.tryPop(value));
    CHECK(value == 1234);
}

TEST_CASE("values come out in the order they went in") {
    MpmcQueue<int> queue(8);

    for (int i = 0; i < 8; ++i) {
        REQUIRE(queue.tryPush(i));
    }

    for (int expected = 0; expected < 8; ++expected) {
        int value = -1;
        REQUIRE(queue.tryPop(value));
        CHECK(value == expected);
    }
}

TEST_CASE("a full queue rejects pushes until something is popped") {
    MpmcQueue<int> queue(4);
    REQUIRE(queue.capacity() == 4);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(queue.tryPush(i));
    }
    CHECK_FALSE(queue.tryPush(99));

    int value = -1;
    REQUIRE(queue.tryPop(value));
    CHECK(value == 0);

    // The freed slot is reusable, which is the wrap-around case.
    CHECK(queue.tryPush(99));
    CHECK_FALSE(queue.tryPush(100));
}

TEST_CASE("slots are reused across many laps") {
    // Far more items than the capacity, so every slot wraps repeatedly. A
    // sequence-number mistake shows up here and nowhere else.
    MpmcQueue<usize> queue(4);

    for (usize i = 0; i < 1000; ++i) {
        REQUIRE(queue.tryPush(i));
        usize value = 0;
        REQUIRE(queue.tryPop(value));
        REQUIRE(value == i);
    }
    CHECK(queue.sizeApprox() == 0);
}

TEST_CASE("moves values rather than copying them") {
    MpmcQueue<std::vector<int>> queue(4);

    std::vector<int> source{1, 2, 3};
    REQUIRE(queue.tryPush(std::move(source)));

    std::vector<int> received;
    REQUIRE(queue.tryPop(received));
    CHECK(received == std::vector<int>{1, 2, 3});
}

TEST_CASE("every item survives many producers and many consumers") {
    // The test the whole data structure exists for. Correctness is checked two
    // ways at once: the count of items received, and their sum. A lost item fails
    // both; a duplicated item fails the sum while the count still matches.
    constexpr usize kProducers = 4;
    constexpr usize kConsumers = 4;
    constexpr usize kPerProducer = 20000;
    constexpr usize kTotal = kProducers * kPerProducer;

    MpmcQueue<usize> queue(1024); // Deliberately far smaller than the item count.

    std::atomic<usize> receivedCount{0};
    std::atomic<usize> receivedSum{0};
    std::atomic<bool> producersDone{false};

    std::vector<std::jthread> threads;
    threads.reserve(kProducers + kConsumers);

    for (usize p = 0; p < kProducers; ++p) {
        threads.emplace_back([&queue, p] {
            for (usize i = 0; i < kPerProducer; ++i) {
                const usize value = p * kPerProducer + i + 1; // 1-based, so 0 is never valid.
                while (!queue.tryPush(value)) {
                    std::this_thread::yield(); // Full: this is the backpressure path.
                }
            }
        });
    }

    for (usize c = 0; c < kConsumers; ++c) {
        threads.emplace_back([&] {
            for (;;) {
                usize value = 0;
                if (queue.tryPop(value)) {
                    REQUIRE(value != 0);
                    receivedSum.fetch_add(value, std::memory_order_relaxed);
                    receivedCount.fetch_add(1, std::memory_order_relaxed);
                } else if (producersDone.load(std::memory_order_acquire)) {
                    // One more attempt closes the race between the last push and
                    // the flag being set.
                    if (!queue.tryPop(value)) {
                        return;
                    }
                    REQUIRE(value != 0);
                    receivedSum.fetch_add(value, std::memory_order_relaxed);
                    receivedCount.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (usize p = 0; p < kProducers; ++p) {
        threads[p].join();
    }
    producersDone.store(true, std::memory_order_release);

    threads.clear(); // Joins the consumers.

    CHECK(receivedCount.load() == kTotal);
    CHECK(receivedSum.load() == kTotal * (kTotal + 1) / 2);
}

TEST_CASE("a single producer and single consumer preserve order under contention") {
    constexpr usize kCount = 50000;

    MpmcQueue<usize> queue(64);
    std::atomic<bool> ordered{true};

    std::jthread consumer([&] {
        usize expected = 0;
        while (expected < kCount) {
            usize value = 0;
            if (!queue.tryPop(value)) {
                std::this_thread::yield();
                continue;
            }
            if (value != expected) {
                ordered.store(false);
                return;
            }
            ++expected;
        }
    });

    for (usize i = 0; i < kCount; ++i) {
        while (!queue.tryPush(i)) {
            std::this_thread::yield();
        }
    }

    consumer.join();
    CHECK(ordered.load());
}
