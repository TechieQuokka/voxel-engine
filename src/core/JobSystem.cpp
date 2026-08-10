#include "core/JobSystem.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "core/Profile.hpp"

#include <format>
#include <string>

namespace mc {
namespace {

constexpr usize kBandCount = static_cast<usize>(JobPriority::Count);

} // namespace

usize JobSystem::defaultWorkerCount() {
    const unsigned hardware = std::thread::hardware_concurrency();

    // hardware_concurrency() may report 0 when it cannot tell. On a machine with
    // very few cores, reserving two of them would leave nothing to work with, so
    // fall back to a single worker rather than to none.
    if (hardware <= 3) {
        return 1;
    }
    return static_cast<usize>(hardware - 2);
}

JobSystem::JobSystem(usize workerCount) {
    const usize count = workerCount != 0 ? workerCount : defaultWorkerCount();

    m_queues.reserve(kBandCount);
    for (usize band = 0; band < kBandCount; ++band) {
        m_queues.push_back(std::make_unique<MpmcQueue<Job>>(kQueueCapacity));
    }

    m_workers.reserve(count);
    for (usize i = 0; i < count; ++i) {
        m_workers.emplace_back([this, i] { workerLoop(i); });
    }

    logInfo("JobSystem: {} workers, {} bands, {} jobs per band",
            count, kBandCount, m_queues.front()->capacity());
}

JobSystem::~JobSystem() {
    m_stopping.store(true, std::memory_order_release);

    // A thread parked in acquire() cannot observe a stop request, so each one has
    // to be handed a permit to wake on. Jobs still queued are dropped: shutdown
    // happens when the world is being torn down, and finishing work whose results
    // nothing will read is pointless.
    m_signal.release(static_cast<std::ptrdiff_t>(m_workers.size()));

    // The jthread members join in their own destructors, after this body.
}

bool JobSystem::submit(JobPriority priority, const Job& job) {
    MC_ASSERT_MSG(job.fn != nullptr, "submitted a job with no function");

    const auto band = static_cast<usize>(priority);
    MC_ASSERT_MSG(band < kBandCount, "invalid JobPriority");

    // Counted before the push, so pending() never reads low while a job is in
    // flight -- waitIdle depends on that being the safe direction to be wrong in.
    m_pending.fetch_add(1, std::memory_order_relaxed);

    if (!m_queues[band]->tryPush(job)) {
        m_pending.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }

    m_signal.release();
    return true;
}

bool JobSystem::tryRunOneJob() {
    Job job;

    // Band order is the enum order, so this scans High before Normal before Low.
    for (const auto& queue : m_queues) {
        if (!queue->tryPop(job)) {
            continue;
        }

        MC_ASSERT(job.fn != nullptr);
        job.fn(job.context, job.payload);
        m_pending.fetch_sub(1, std::memory_order_release);
        return true;
    }
    return false;
}

void JobSystem::workerLoop(usize index) {
    const std::string threadName = std::format("mc-worker-{}", index);
    MC_PROFILE_THREAD(threadName.c_str());

    for (;;) {
        m_signal.acquire();

        if (m_stopping.load(std::memory_order_acquire)) {
            return;
        }

        // Permits and jobs are one to one -- submit releases exactly once, after
        // the push is visible -- so a job is normally waiting. Treating a failure
        // as a spurious wake rather than asserting the invariant keeps a mistake
        // in that reasoning from deadlocking the pool.
        if (!tryRunOneJob()) {
            continue;
        }
    }
}

void JobSystem::waitIdle() {
    while (m_pending.load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
}

} // namespace mc
