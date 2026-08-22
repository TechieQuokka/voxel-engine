#include "core/JobSystem.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <vector>

using namespace mc;

namespace {

/// Jobs carry a context pointer and a u64, so a counter is all the state a test
/// needs. Free functions rather than lambdas because Job::Fn is a plain function
/// pointer -- which is the whole point of the design.
void incrementCounter(void* context, u64 /*payload*/) {
    static_cast<std::atomic<usize>*>(context)->fetch_add(1, std::memory_order_relaxed);
}

void addPayload(void* context, u64 payload) {
    static_cast<std::atomic<u64>*>(context)->fetch_add(payload, std::memory_order_relaxed);
}

/// A meeting every worker has to reach before any of them may leave.
///
/// **Counting distinct thread ids over a flood of quick jobs does not prove
/// parallelism.** One worker draining the whole queue before the others wake from
/// the semaphore is a legitimate schedule, and on a loaded machine it is a common
/// one -- that version of this test failed about one run in three once the suite
/// started running cases concurrently. Holding each worker until all of them have
/// arrived turns the claim into a fact the scheduler cannot take away.
struct Rendezvous {
    std::atomic<usize> arrived{0};
    usize expected = 0;
    std::mutex mutex;
    std::set<std::thread::id> ids;
};

void rendezvous(void* context, u64 /*payload*/) {
    auto* meeting = static_cast<Rendezvous*>(context);
    {
        const std::lock_guard<std::mutex> lock(meeting->mutex);
        meeting->ids.insert(std::this_thread::get_id());
    }
    meeting->arrived.fetch_add(1, std::memory_order_acq_rel);

    // Bounded, so a pool that cannot deliver the parallelism fails the assertion
    // rather than hanging the suite. Generous, because the deadline is not the
    // thing under test and a busy CI machine should not trip it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (meeting->arrived.load(std::memory_order_acquire) < meeting->expected
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
}

} // namespace

TEST_CASE("defaultWorkerCount reserves the main and upload threads") {
    // Whatever the machine reports, the pool must never be empty.
    CHECK(JobSystem::defaultWorkerCount() >= 1);

    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware > 3) {
        CHECK(JobSystem::defaultWorkerCount() == static_cast<usize>(hardware - 2));
    }
}

TEST_CASE("an explicit worker count is honoured") {
    JobSystem jobs(3);
    CHECK(jobs.workerCount() == 3);
}

TEST_CASE("every submitted job runs exactly once") {
    constexpr usize kJobCount = 5000;

    std::atomic<usize> counter{0};

    {
        JobSystem jobs(4);
        for (usize i = 0; i < kJobCount; ++i) {
            REQUIRE(jobs.submit(JobPriority::Normal,
                                Job{&incrementCounter, &counter, 0}));
        }
        jobs.waitIdle();
        CHECK(jobs.pending() == 0);
    }

    CHECK(counter.load() == kJobCount);
}

TEST_CASE("the payload reaches the job untouched") {
    std::atomic<u64> total{0};

    {
        JobSystem jobs(2);
        for (u64 i = 1; i <= 1000; ++i) {
            REQUIRE(jobs.submit(JobPriority::Normal, Job{&addPayload, &total, i}));
        }
        jobs.waitIdle();
    }

    CHECK(total.load() == 1000 * 1001 / 2);
}

TEST_CASE("all three bands are drained") {
    std::atomic<usize> counter{0};

    {
        JobSystem jobs(2);
        for (const JobPriority priority :
             {JobPriority::High, JobPriority::Normal, JobPriority::Low}) {
            for (usize i = 0; i < 100; ++i) {
                REQUIRE(jobs.submit(priority, Job{&incrementCounter, &counter, 0}));
            }
        }
        jobs.waitIdle();
    }

    CHECK(counter.load() == 300);
}

TEST_CASE("a full band reports backpressure instead of growing") {
    // One worker, blocked, so nothing drains while the band fills up.
    std::atomic<bool> release{false};
    std::atomic<usize> started{0};

    struct Gate {
        std::atomic<bool>* release;
        std::atomic<usize>* started;
    };
    Gate gate{&release, &started};

    const auto block = [](void* context, u64) {
        auto* g = static_cast<Gate*>(context);
        g->started->fetch_add(1, std::memory_order_relaxed);
        while (!g->release->load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    };

    JobSystem jobs(1);

    // The first job occupies the single worker.
    REQUIRE(jobs.submit(JobPriority::Normal, Job{block, &gate, 0}));
    while (started.load(std::memory_order_relaxed) == 0) {
        std::this_thread::yield();
    }

    std::atomic<usize> counter{0};
    usize accepted = 0;
    bool refused = false;
    for (usize i = 0; i < JobSystem::kQueueCapacity + 16; ++i) {
        if (jobs.submit(JobPriority::Normal, Job{&incrementCounter, &counter, 0})) {
            ++accepted;
        } else {
            refused = true;
            break;
        }
    }

    CHECK(refused);
    CHECK(accepted <= JobSystem::kQueueCapacity);

    release.store(true, std::memory_order_release);
    jobs.waitIdle();
    CHECK(counter.load() == accepted);
}

TEST_CASE("jobs run on worker threads, not on the submitting thread") {
    constexpr usize kWorkers = 4;

    Rendezvous meeting;
    meeting.expected = kWorkers;

    {
        JobSystem jobs(kWorkers);
        // One job per worker. Each holds the worker that took it until all of them
        // have arrived, so a worker cannot take a second and the ids that turn up
        // are every worker in the pool rather than however many happened to wake.
        for (usize i = 0; i < kWorkers; ++i) {
            REQUIRE(jobs.submit(JobPriority::Normal, Job{&rendezvous, &meeting, 0}));
        }
        jobs.waitIdle();
    }

    const std::lock_guard<std::mutex> lock(meeting.mutex);
    CHECK(meeting.ids.count(std::this_thread::get_id()) == 0);
    CHECK(meeting.ids.size() == kWorkers);
}

TEST_CASE("destruction with work still queued does not hang") {
    // Shutdown drops queued jobs by design. What matters is that it terminates:
    // workers parked on the semaphore have to be woken to see the stop flag.
    std::atomic<usize> counter{0};

    {
        JobSystem jobs(2);
        for (usize i = 0; i < 4000; ++i) {
            jobs.submit(JobPriority::Low, Job{&incrementCounter, &counter, 0});
        }
    }

    CHECK(counter.load() <= 4000);
}

TEST_CASE("an idle pool starts and stops cleanly") {
    JobSystem jobs(4);
    CHECK(jobs.pending() == 0);
    jobs.waitIdle(); // Must return immediately rather than wait for a job.
}
