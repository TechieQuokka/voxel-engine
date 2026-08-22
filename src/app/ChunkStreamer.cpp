#include "app/ChunkStreamer.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "core/Profile.hpp"
#include "mesh/BinaryGreedyMesher.hpp"
#include "world/BlockLight.hpp"

#include <algorithm>
#include <cstdint>
#include <stop_token>
#include <thread>
#include <utility>

namespace mc {
namespace {

/// How much work the main thread hands to the pool per frame.
///
/// These bound *submission*, not execution -- the pool does the work, and the frame
/// loop never waits for it. They exist because submitting is not free: each meshing
/// job gathers a neighbourhood and pins nine columns, so an unbounded submit on the
/// frame a region reloads would put thousands of those in one frame.
///
/// Measured at distance 24, flying at 40 blocks/s, these keep the loaded set complete
/// -- nothing left generating at the end of a 20-second run -- so they are not the
/// limiting factor and there is no reason to raise them.
constexpr usize kColumnsPerFrame = 16;
constexpr usize kSectionsPerFrame = 32;

} // namespace

ChunkStreamer::ChunkStreamer(World& world, Generator& generator, WorldStore* store,
                             SectionMeshStore& meshStore, i32 renderDistance)
    : m_world(world), m_generator(generator), m_store(store), m_meshStore(meshStore),
      m_renderDistance(renderDistance) {
    // Sized and filled before any thread exists, and never resized afterwards:
    // indices into it travel through queues to other threads.
    m_meshTasks.resize(kMeshTaskPoolSize);
    m_freeMeshTasks = std::make_unique<MpmcQueue<u32>>(kMeshTaskPoolSize);
    m_uploadQueue = std::make_unique<MpmcQueue<u32>>(kMeshTaskPoolSize);
    for (u32 index = 0; index < kMeshTaskPoolSize; ++index) {
        const bool pushed = m_freeMeshTasks->tryPush(index);
        MC_VERIFY(pushed);
    }

    m_jobs = std::make_unique<JobSystem>();
    startUploadThread();
}

ChunkStreamer::~ChunkStreamer() {
    shutdown();
}

void ChunkStreamer::shutdown() {
    // Order matters. Workers hold task indices and Chunk pins, so they have to stop
    // before the upload thread that recycles what they produce, and both have to
    // stop before the World and the mesh store go away.
    if (m_jobs) {
        m_jobs.reset(); // Joins the workers; queued jobs are dropped.
    }

    if (m_uploadThread.joinable()) {
        m_uploadStopping.store(true, std::memory_order_release);
        m_uploadSignal.release();
        m_uploadThread.join();
    }
}

void ChunkStreamer::startUploadThread() {
    // **The stop request has to open the same door `shutdown` does.**
    //
    // `uploadLoop` parks in `m_uploadSignal.acquire()` and leaves only on
    // `m_uploadStopping`, so a bare `request_stop()` is invisible to it. That is fine
    // on the ordinary path, where the destructor runs `shutdown` first -- and it hangs
    // on the path where the destructor never runs: a throw part-way through
    // constructing whatever owns this streamer destroys the members without the
    // destructor body, and `~jthread` then joins a thread nothing has told to stop.
    //
    // `JobSystem` solves the same problem in its destructor body (JobSystem.cpp:46).
    // This thread is not owned by a type of its own, so the callback is where it goes.
    m_uploadThread = std::jthread([this](std::stop_token stop) {
        const std::stop_callback wake(stop, [this] {
            m_uploadStopping.store(true, std::memory_order_release);
            m_uploadSignal.release();
        });
        uploadLoop();
    });
}

JobPriority ChunkStreamer::priorityFor(ChunkPos pos) const {
    const i32 distance =
        std::max(std::abs(pos.x - m_centre.x), std::abs(pos.z - m_centre.z));
    const i32 renderDistance = std::max(1, m_renderDistance);

    // Thirds of the render distance. The bands only have to be roughly right: they
    // decide what gets done first, and the camera keeps changing the answer anyway.
    if (distance * 3 <= renderDistance) {
        return JobPriority::High;
    }
    if (distance * 3 <= renderDistance * 2) {
        return JobPriority::Normal;
    }
    return JobPriority::Low;
}

bool ChunkStreamer::neighboursReady(ChunkPos pos) const {
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            const Chunk* neighbour = m_world.find(ChunkPos{pos.x + dx, pos.z + dz});
            if (neighbour == nullptr || neighbour->state() != ChunkState::Ready) {
                return false;
            }
        }
    }
    return true;
}

void ChunkStreamer::queueRelight(ChunkPos column) {
    const std::lock_guard<std::mutex> guard(m_relightMutex);
    m_relightQueue.push_back(column);
}

std::vector<ChunkPos> ChunkStreamer::takeRelightQueue() {
    std::vector<ChunkPos> arrived;
    const std::lock_guard<std::mutex> guard(m_relightMutex);
    arrived.swap(m_relightQueue);
    return arrived;
}

void ChunkStreamer::requeueRelight(const std::vector<ChunkPos>& columns) {
    if (columns.empty()) {
        return;
    }
    const std::lock_guard<std::mutex> guard(m_relightMutex);
    m_relightQueue.insert(m_relightQueue.end(), columns.begin(), columns.end());
}

std::vector<SavedFurnace> ChunkStreamer::takeLoadedFurnaces() {
    std::vector<SavedFurnace> loaded;
    const std::lock_guard<std::mutex> guard(m_loadedFurnaceMutex);
    loaded.swap(m_loadedFurnaces);
    return loaded;
}

ChunkStreamer::Stats ChunkStreamer::stats() const {
    return Stats{
        m_sectionsMeshed.load(std::memory_order_relaxed),
        m_sectionsEmpty.load(std::memory_order_relaxed),
        m_arenaFullEvents.load(std::memory_order_relaxed),
    };
}

void ChunkStreamer::generateColumnJob(void* context, u64 payload) {
    MC_PROFILE_SCOPE_N("generateColumnJob");

    auto* streamer = static_cast<ChunkStreamer*>(context);
    auto* chunk = reinterpret_cast<Chunk*>(static_cast<std::uintptr_t>(payload));

    // **Loading happens here rather than on the main thread, and the reason is the
    // same one that puts generation here**: this is the one point where a single
    // thread owns the column and nothing else can see it. The `Generating` state it
    // was put in before submission is what keeps the World from unloading it, and
    // what makes `World::blockAt` answer air for it in the meantime.
    if (streamer->m_store) {
        std::vector<SavedFurnace> furnaces;
        const Result<bool, WorldStore::Error> loaded =
            streamer->m_store->loadColumn(*chunk, &furnaces);

        // A value of false means nobody edited this column, which is the ordinary
        // case; an error means it is on disk and unreadable, and both regenerate.
        // Regenerating over a half-decoded column is safe: every branch of
        // `generateColumn` fills the section it is about to write.
        if (loaded.hasValue() && loaded.value()) {
            if (!furnaces.empty()) {
                const std::lock_guard<std::mutex> guard(streamer->m_loadedFurnaceMutex);
                streamer->m_loadedFurnaces.insert(streamer->m_loadedFurnaces.end(),
                                                  furnaces.begin(), furnaces.end());
            }

            // A column off disk is written back when it unloads, because a furnace
            // in it burns down while nobody is looking and nothing else would say
            // its timers moved.
            chunk->markEdited();

            // Before Ready, so the flag is set by the time anything can look at the
            // column. **This is the branch that matters**: a torch only ever gets
            // into the world by being placed, so a column off disk is the only one
            // that can arrive already holding one.
            noteEmitters(*chunk);

            // The same tail `generateColumn` ends on, so a loaded column and a
            // generated one arrive in exactly the same state.
            chunk->markAllDirty();
            chunk->setState(ChunkState::Ready);
            streamer->queueRelight(chunk->position());
            return;
        }
    }

    // Sets the column Ready and marks every section dirty when it finishes.
    streamer->m_generator.generateColumn(*chunk);

    // Always false today -- nothing the generator places emits. Asked anyway, because
    // the day something does (lava) the cost of having forgotten is light that never
    // appears, and the check is twelve palette scans on a worker that has just
    // written every voxel in the column.
    noteEmitters(*chunk);
    streamer->queueRelight(chunk->position());
}

void ChunkStreamer::meshSectionJob(void* context, u64 payload) {
    MC_PROFILE_SCOPE_N("meshSectionJob");

    auto* streamer = static_cast<ChunkStreamer*>(context);
    const auto index = static_cast<usize>(payload);
    MeshTask& task = streamer->m_meshTasks[index];

    meshSectionGreedy(task.hood, task.mesh);

    // The upload queue is at least as large as the task pool, so there is always
    // room -- a slot cannot be in flight without having come from the pool.
    const bool pushed = streamer->m_uploadQueue->tryPush(static_cast<u32>(index));
    MC_VERIFY_MSG(pushed, "upload queue smaller than the mesh task pool");

    streamer->m_uploadSignal.release();
}

usize ChunkStreamer::submitGeneration() {
    MC_PROFILE_SCOPE_N("ChunkStreamer::submitGeneration");

    usize submitted = 0;
    m_world.forEachChunk([&](Chunk& chunk) {
        if (submitted >= kColumnsPerFrame || chunk.state() != ChunkState::Empty) {
            return;
        }

        // Claimed before submitting: the World must not unload a column a worker is
        // about to write into, and Generating is what tells it so.
        chunk.setState(ChunkState::Generating);

        const Job job{&generateColumnJob, this,
                      static_cast<u64>(reinterpret_cast<std::uintptr_t>(&chunk))};

        if (!m_jobs->submit(priorityFor(chunk.position()), job)) {
            chunk.setState(ChunkState::Empty); // Band full; try again next frame.
            return;
        }
        ++submitted;
    });
    return submitted;
}

usize ChunkStreamer::submitMeshing() {
    MC_PROFILE_SCOPE_N("ChunkStreamer::submitMeshing");

    usize submitted = 0;

    m_world.forEachChunk([&](Chunk& chunk) {
        if (submitted >= kSectionsPerFrame || !chunk.anyDirty()) {
            return;
        }
        if (!neighboursReady(chunk.position())) {
            return;
        }

        for (usize index = 0; index < Chunk::kSectionCount && submitted < kSectionsPerFrame;
             ++index) {
            if (!chunk.isSectionDirty(index)) {
                continue;
            }

            const SectionPos pos{chunk.position().x,
                                 kMinSectionY + static_cast<i32>(index),
                                 chunk.position().z};

            if (chunk.sectionByIndex(index).isEmpty()) {
                // Nothing to draw and nothing to keep -- the common case above the
                // surface. No job needed.
                m_meshStore.release(pos, m_frame);
                chunk.clearSectionDirty(index);
                continue;
            }

            u32 taskIndex = 0;
            if (!m_freeMeshTasks->tryPop(taskIndex)) {
                return; // Pool exhausted. Backpressure, not an error.
            }

            MeshTask& task = m_meshTasks[taskIndex];
            task.pos = pos;
            task.hood = m_world.neighbourhood(pos);

            // Pin every column the neighbourhood points into, for as long as the
            // task lives -- which is until the upload thread is done with it, not
            // until the mesher returns.
            usize slot = 0;
            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 dx = -1; dx <= 1; ++dx) {
                    Chunk* neighbour = m_world.find(ChunkPos{pos.x + dx, pos.z + dz});
                    task.pinned[slot++] = neighbour;
                    if (neighbour != nullptr) {
                        neighbour->pin();
                    }
                }
            }
            MC_ASSERT(task.pinned[kCentrePinSlot] == &chunk);

            // Cleared before submitting rather than after: if something dirties the
            // section again while the job runs, that has to be noticed and remeshed,
            // not swallowed by a clear that happens later.
            chunk.clearSectionDirty(index);

            const Job job{&meshSectionJob, this, static_cast<u64>(taskIndex)};
            if (!m_jobs->submit(priorityFor(chunk.position()), job)) {
                for (Chunk* pinned : task.pinned) {
                    if (pinned != nullptr) {
                        pinned->unpin();
                    }
                }
                task.pinned.fill(nullptr);
                chunk.markSectionDirty(index);

                const bool returned = m_freeMeshTasks->tryPush(taskIndex);
                MC_VERIFY_MSG(returned, "free-task queue smaller than the pool");
                return;
            }
            ++submitted;
        }
    });

    return submitted;
}

void ChunkStreamer::uploadLoop() {
    MC_PROFILE_THREAD("upload");

    for (;;) {
        m_uploadSignal.acquire();

        if (m_uploadStopping.load(std::memory_order_acquire)) {
            return;
        }

        u32 index = 0;
        if (!m_uploadQueue->tryPop(index)) {
            continue; // Spurious wake; permits and pushes are otherwise one to one.
        }

        MeshTask& task = m_meshTasks[index];

        // The whole of the upload: a memcpy into a persistently mapped, coherent
        // buffer. No GL call, which is exactly why this thread needs no context.
        m_sectionsMeshed.fetch_add(1, std::memory_order_relaxed);
        if (task.mesh.empty()) {
            m_sectionsEmpty.fetch_add(1, std::memory_order_relaxed);
        }

        const u64 frame = m_frameForUpload.load(std::memory_order_relaxed);
        if (!m_meshStore.store(task.pos, task.mesh, frame)) {
            // Arena full. Put the section back on the dirty list -- while its column
            // is still pinned, so it is certainly still alive -- and let the main
            // thread resubmit once recycle() has returned some ranges.
            m_arenaFullEvents.fetch_add(1, std::memory_order_relaxed);
            if (Chunk* owner = task.pinned[kCentrePinSlot]) {
                owner->markSectionDirty(
                    static_cast<usize>(sectionIndexInColumn(task.pos.y)));
            }
        }

        for (Chunk* pinned : task.pinned) {
            if (pinned != nullptr) {
                pinned->unpin();
            }
        }
        task.pinned.fill(nullptr);

        const bool returned = m_freeMeshTasks->tryPush(index);
        MC_VERIFY_MSG(returned, "free-task queue smaller than the pool");
    }
}

void ChunkStreamer::drain() {
    // Submit, let the pool finish, repeat. Generation produces meshing work, so this
    // needs more than one pass; it settles when a full round submits nothing.
    //
    // The round counter is not paranoia. A full arena makes store() fail, which marks
    // the section dirty again, which makes the next round submit it again -- a loop that
    // never terminates and never says why. That happened the first time caves ran at
    // distance 16, because the arena budget still reflected pre-cave mesh sizes.
    constexpr usize kMaxRounds = 512;

    for (usize round = 0; round < kMaxRounds; ++round) {
        const usize generated = submitGeneration();
        const usize meshedSubmitted = submitMeshing();

        m_jobs->waitIdle();

        // Wait for the upload thread to drain what those jobs produced. Nothing else
        // is running by now, so an empty queue means finished.
        while (m_uploadQueue->sizeApprox() != 0) {
            std::this_thread::yield();
        }

        if (generated == 0 && meshedSubmitted == 0) {
            return;
        }
    }

    logWarn("Warm-up gave up after {} rounds: arena {} / {} MiB, largest free block {} KiB, "
            "{} arena-full events -- the mesh arena is too small for this render distance",
            kMaxRounds,
            m_meshStore.usedBytes() / (1024 * 1024),
            m_meshStore.capacityBytes() / (1024 * 1024),
            m_meshStore.largestFreeBlock() / 1024,
            m_arenaFullEvents.load());
}

} // namespace mc
