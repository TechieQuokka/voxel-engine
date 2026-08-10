#pragma once

#include "core/MpmcQueue.hpp"
#include "core/Types.hpp"

#include <atomic>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

namespace mc {

/// A unit of work for the worker pool.
///
/// A plain struct rather than std::function, for two reasons.
///
/// Layering: `core` must not know what a section is. A type-erased callable would
/// let world types in through the template, and the point of the dependency rules
/// in DESIGN.md 5.1 is that they are enforced rather than merely intended.
///
/// Cost: a lambda capturing a `World*` and a `SectionPos` is 20 bytes, which
/// overruns libstdc++'s 16-byte std::function small-object buffer. That would put
/// a heap allocation on the submit path, once per section, thousands of times a
/// second while streaming.
///
/// A job must not throw. Exceptions are confined to init and load boundaries
/// (DESIGN.md 6.2), so an exception escaping here is a bug, and it would
/// terminate the process rather than be swallowed.
struct Job {
    using Fn = void (*)(void* context, u64 payload);

    Fn fn = nullptr;
    void* context = nullptr;
    u64 payload = 0;
};

/// Which band a job is queued in. Workers always drain the more urgent band
/// first, so a chunk about to enter the frustum overtakes a prefetch.
///
/// Bands rather than a distance-sorted priority queue: sorting is not lock-free,
/// and any ordering computed at submit time is stale as soon as the camera moves.
/// Re-prioritising is therefore the caller's business -- it submits into the band
/// that is right now, and a job whose chunk is no longer wanted checks that and
/// returns immediately when it runs.
enum class JobPriority : u32 {
    High = 0,   ///< Inside or entering the frustum.
    Normal = 1, ///< Within the render distance.
    Low = 2,    ///< Prefetch beyond it.
    Count,
};

/// Fixed pool of worker threads sharing one bounded queue per priority band.
///
/// One pool for both generation and meshing, per DESIGN.md 3.13 -- the meshing
/// pool is described there as sharing the generation pool's threads, and splitting
/// them would risk one sitting idle while the other saturates. The upload thread
/// from 3.13 is *not* here: it needs a GL context, and `core` knows nothing about
/// GL, so it belongs to the render layer.
///
/// **The main thread never blocks on this.** `submit` is wait-free apart from the
/// queue's CAS retry, and there is deliberately no "wait for this job" call --
/// results come back through a queue the caller owns.
class JobSystem {
public:
    /// Jobs per band. At render distance 16 a full reload is ~13,000 sections, so
    /// this does not attempt to hold the whole world -- it holds a frame or two of
    /// work, and backpressure covers the rest.
    static constexpr usize kQueueCapacity = 8192;

    /// hardware_concurrency() minus one for the main thread and one for the
    /// upload thread, per DESIGN.md 3.13. Never less than 1.
    static usize defaultWorkerCount();

    /// `workerCount` of 0 means defaultWorkerCount().
    explicit JobSystem(usize workerCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /// Returns false when the band is full. That is backpressure, not an error:
    /// the caller drops the request and re-submits next frame, by which time the
    /// right priority may have changed anyway.
    bool submit(JobPriority priority, const Job& job);

    usize workerCount() const noexcept { return m_workers.size(); }

    /// Submitted but not yet finished, across all bands.
    usize pending() const noexcept { return m_pending.load(std::memory_order_acquire); }

    /// Blocks until everything submitted so far has run.
    ///
    /// For tests and orderly shutdown only. Calling this from the frame loop
    /// would defeat the entire purpose of the pool.
    void waitIdle();

private:
    void workerLoop(usize index);
    /// Takes one job from the most urgent non-empty band and runs it.
    bool tryRunOneJob();

    /// unique_ptr because MpmcQueue owns atomics and is therefore immovable,
    /// which a vector of values would require.
    std::vector<std::unique_ptr<MpmcQueue<Job>>> m_queues;

    std::counting_semaphore<> m_signal{0};
    std::atomic<usize> m_pending{0};
    std::atomic<bool> m_stopping{false};

    /// Declared last: workers start running in their constructor and must not
    /// observe a half-built queue set.
    std::vector<std::jthread> m_workers;
};

} // namespace mc
