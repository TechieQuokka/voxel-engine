#pragma once

#include "core/JobSystem.hpp"
#include "core/MpmcQueue.hpp"
#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "render/SectionMeshStore.hpp"
#include "world/Chunk.hpp"
#include "world/Neighbourhood.hpp"
#include "world/World.hpp"
#include "world/WorldStore.hpp"
#include "worldgen/Generator.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

namespace mc {

/// Everything between "the camera is here" and "the meshes are in the arena".
///
/// **This was Engine's, and the split is what makes the thread lifetime honest.**
/// The pool, the task ring, the upload thread and the two worker-to-main handoffs are
/// one mechanism with one shutdown order, and while they were members of a class that
/// also owned the window, the player and the HUD, that order was maintained by a
/// destructor body sixty methods away from them. Here the destructor is three lines and
/// stands next to the things it stops.
///
/// The streamer borrows: the World it fills, the Generator it fills it from, the store
/// it loads edited columns out of, and the arena it uploads into. All four outlive it,
/// which the Engine guarantees by declaring this after them.
///
/// **Threading.** `submitGeneration`, `submitMeshing`, `drain` and the accessors are
/// main-thread only. Workers run `generateColumnJob` and `meshSectionJob`; the upload
/// thread runs `uploadLoop`. What crosses between them is the two queues, the atomics
/// below, and the two mutex-guarded handoff vectors -- nothing else.
class ChunkStreamer {
public:
    ChunkStreamer(World& world, Generator& generator, WorldStore* store,
                  SectionMeshStore& meshStore, i32 renderDistance);

    /// Stops the workers and the upload thread. Safe to have already run `shutdown()`.
    ~ChunkStreamer();

    ChunkStreamer(const ChunkStreamer&) = delete;
    ChunkStreamer& operator=(const ChunkStreamer&) = delete;

    /// The column priority is measured from. Set when the camera crosses a boundary.
    void setCentre(ChunkPos centre) { m_centre = centre; }

    /// The frame number retired arena ranges are stamped with. Once a frame.
    void setFrame(u64 frame) {
        m_frame = frame;
        m_frameForUpload.store(frame, std::memory_order_relaxed);
    }

    /// Hands columns needing generation to the worker pool. Returns how many were
    /// submitted, not how many finished -- nothing here waits.
    usize submitGeneration();

    /// Hands dirty sections to the worker pool, with their neighbourhood gathered and
    /// the nine columns it points into pinned.
    usize submitMeshing();

    /// Runs the pipeline until nothing is outstanding. Used by --warm-up and by
    /// captures; never by the frame loop, which must not block.
    void drain();

    /// Stops the workers and then the upload thread, in that order. Called by the
    /// Engine before the World and the arena go away, and again by the destructor.
    void shutdown();

    /// True when no submitted work is outstanding. Only meaningful right after a
    /// submit pass that produced nothing.
    bool idle() const { return !m_jobs || m_jobs->pending() == 0; }

    usize workerCount() const { return m_jobs ? m_jobs->workerCount() : 0; }

    /// Furnaces a worker read off disk, waiting for the main thread to adopt them.
    ///
    /// **The handoff exists because loading happens on a worker and the furnace map
    /// belongs to the main thread.** A column is loaded inside its generation job --
    /// the one point where exactly one thread owns it and nothing else can see it --
    /// so the voxels can be written straight in, but the furnaces cannot: they go
    /// into a map the simulation walks every tick.
    std::vector<SavedFurnace> takeLoadedFurnaces();

    /// Columns that have just become Ready and have not been relit yet.
    ///
    /// **The same handoff as the furnaces, for the same reason and one more.** Block
    /// light is derived and therefore not saved, so a column arrives with its torches
    /// and no light around them -- but relighting it writes into its neighbours, which
    /// a worker owning one column has no right to touch. So the worker records the
    /// position and the main thread does the flood.
    std::vector<ChunkPos> takeRelightQueue();

    /// Puts back columns the main thread could not relight yet, because a mesher was
    /// holding one of the nine they touch.
    void requeueRelight(const std::vector<ChunkPos>& columns);

    struct Stats {
        /// Meshing jobs completed. Deliberately separate from the mesh store's section
        /// count: a section entirely inside solid rock has every face hidden by its
        /// neighbours, so it is meshed, produces zero quads, and is stored nowhere. The
        /// gap between these two numbers is how much of the world costs nothing to draw.
        usize sectionsMeshed;
        usize sectionsEmpty;
        usize arenaFullEvents;
    };
    Stats stats() const;

private:
    /// A meshing job's borrowed state. Built on the main thread, filled by a worker,
    /// consumed by the upload thread, then returned to the free list.
    ///
    /// Pooled rather than allocated per job for two reasons: `Job` carries a `u64`,
    /// so an index is what fits, and the pooled ChunkMesh keeps its vector capacity
    /// between uses, which removes the per-section allocation from the mesh path
    /// entirely once the pool is warm.
    struct MeshTask {
        SectionPos pos{};
        SectionNeighbourhood hood;
        /// The nine columns the neighbourhood points into, pinned for the task's
        /// whole life so the World cannot unload one underneath it. Indexed
        /// (dz + 1) * 3 + (dx + 1), so the centre is 4.
        std::array<Chunk*, 9> pinned{};
        ChunkMesh mesh;
    };

    static constexpr usize kCentrePinSlot = 4;

    /// In-flight meshing tasks. Sized once, before any thread starts, and never
    /// resized -- indices into it travel through queues.
    static constexpr usize kMeshTaskPoolSize = 1024;

    /// Job entry points. Static members rather than free functions so they can reach
    /// private state; `Job::Fn` is a plain function pointer either way.
    static void generateColumnJob(void* context, u64 payload);
    static void meshSectionJob(void* context, u64 payload);

    /// Which band a piece of work goes in, from its distance to the camera column.
    JobPriority priorityFor(ChunkPos pos) const;

    /// True when every column of `pos`'s 3x3 neighbourhood holds generated voxels.
    ///
    /// Meshing before that would cull the section's boundary faces against columns
    /// that are still empty, and the result would have to be thrown away. Waiting
    /// also removes the need to remesh on arrival entirely: a section is never
    /// meshed against a neighbour that is about to change.
    bool neighboursReady(ChunkPos pos) const;

    void startUploadThread();
    void uploadLoop();

    /// Records a column as needing block light. Called from a worker, after the
    /// release store that makes the column Ready.
    void queueRelight(ChunkPos column);

    World& m_world;
    Generator& m_generator;
    /// Null when `--no-save` was given, and every call site tests for that.
    WorldStore* m_store;
    SectionMeshStore& m_meshStore;

    i32 m_renderDistance;
    ChunkPos m_centre{};
    u64 m_frame = 0;

    std::unique_ptr<JobSystem> m_jobs;
    std::vector<MeshTask> m_meshTasks;
    /// Indices of unused tasks. Popping one is how the main thread reserves a slot,
    /// and an empty queue is backpressure: it stops submitting this frame.
    std::unique_ptr<MpmcQueue<u32>> m_freeMeshTasks;
    /// Finished meshes waiting to be copied into the arena. At least as large as the
    /// task pool, so a worker's push can never fail.
    std::unique_ptr<MpmcQueue<u32>> m_uploadQueue;

    std::counting_semaphore<> m_uploadSignal{0};
    std::atomic<bool> m_uploadStopping{false};
    /// The frame number the upload thread stamps retired ranges with.
    std::atomic<u64> m_frameForUpload{0};
    std::atomic<usize> m_arenaFullEvents{0};
    std::atomic<usize> m_sectionsMeshed{0};
    std::atomic<usize> m_sectionsEmpty{0};

    std::mutex m_loadedFurnaceMutex;
    std::vector<SavedFurnace> m_loadedFurnaces;

    std::mutex m_relightMutex;
    std::vector<ChunkPos> m_relightQueue;

    /// Declared after everything it touches, so it is joined before any of it is
    /// destroyed. `shutdown()` still signals it explicitly first, because a thread
    /// parked on a semaphore cannot observe a destructor -- and the jthread callback
    /// in `startUploadThread` covers the path where `shutdown()` never runs at all.
    std::jthread m_uploadThread;
};

} // namespace mc
