#pragma once

#include "core/JobSystem.hpp"
#include "core/MpmcQueue.hpp"
#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/CharacterRenderer.hpp"
#include "render/ChunkRenderer.hpp"
#include "render/SectionMeshStore.hpp"
#include "rhi/Device.hpp"
#include "world/World.hpp"
#include "worldgen/Generator.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

namespace mc {

/// Owns the window, the graphics device, the world, and the frame loop.
class Engine {
public:
    struct Options {
        /// When set, renders a single frame, writes it to this path as a PNM
        /// image, and exits. Lets rendering be verified without a compositor
        /// screenshot, which is also how reference captures get taken in later
        /// phases.
        std::string capturePath;

        /// Times the meshers against each other before starting. Off by default
        /// because it meshes the section several hundred times, which has no
        /// business happening on the path to a running frame -- but the AO merge
        /// measurement has to be repeated once terrain has caves and overhangs,
        /// so the code stays.
        bool meshBenchmark = false;

        /// Square radius in chunk columns. Phase 3's exit criterion is 16.
        i32 renderDistance = 16;

        /// Third person by default, so the character is visible without pressing
        /// anything. F5 toggles it at runtime.
        bool thirdPerson = true;

        /// Start in fly mode rather than walking. Walking cannot reach a cave, so
        /// anything inspecting the underground wants this.
        bool flying = false;

        /// Streams the whole region in before the first frame and logs how long it
        /// took, instead of filling in over several seconds. For measurement.
        bool warmUp = false;

        /// Flies the camera for this many **real** seconds with vsync off and no
        /// cursor capture, then reports the frame-time distribution and exits.
        ///
        /// Vsync makes a frame-time criterion unmeasurable -- every frame reads as
        /// 16.7 ms whatever it cost -- so the benchmark turns it off.
        ///
        /// Wall-clock seconds, and the camera advances by *measured* delta time, for
        /// a reason that invalidated an earlier version of this. Advancing by a fixed
        /// 1/60 step while rendering at 4,000 FPS moved the camera at 66x real speed:
        /// streaming could not keep up, 162 of 289 columns sat unfinished, and the
        /// visible set emptied out. That biases the two halves of the measurement in
        /// opposite directions -- streaming submission too heavy, rendering too light
        /// -- and the result means nothing. Tying motion to real time costs
        /// reproducibility of the exact path and buys a number that describes a player.
        f64 benchSeconds = 0.0;
    };

    /// No default argument: a nested struct's default member initializers are
    /// parsed only after the enclosing class is complete, so `= {}` here cannot
    /// see them. main always constructs an Options.
    explicit Engine(Options options);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    /// Meshes a test section with each strategy and logs the comparison. Only runs
    /// under Options::meshBenchmark.
    void runMeshBenchmark();

    /// Rebuilds the projection from the current framebuffer size. Called at
    /// startup and on every resize, so the two cannot drift apart.
    void updateProjection();
    void updateCamera(f64 deltaTime);

    /// Walking: horizontal input only, gravity, and a step the player can climb.
    void updateWalk(f32 dt);
    /// Free flight: the original debug camera, kept because walking cannot get
    /// underground and the caves are the most interesting thing down there.
    void updateFly(f32 dt);

    /// Height a player standing at (x, z) would have their feet at, taking the
    /// highest solid block at or below `fromY`. Empty when the column has not
    /// streamed in yet, or there is nothing solid within reach.
    std::optional<f32> groundBelow(f32 x, f32 z, f32 fromY) const;

    /// Points `m_renderCamera` at wherever the frame should be seen from: the
    /// player's eye in first person, a few blocks behind it in third.
    void updateRenderCamera();

    ChunkPos cameraColumn() const;

    /// Loads and unloads columns around the camera. Only does work when the camera
    /// has crossed into a different column.
    void updateLoadedRegion();

    /// Hands columns needing generation to the worker pool. Returns how many were
    /// submitted, not how many finished -- nothing here waits.
    usize submitGeneration();
    /// Hands dirty sections to the worker pool, with their neighbourhood gathered
    /// and the nine columns it points into pinned.
    usize submitMeshing();

    /// Which band a piece of work goes in, from its distance to the camera column.
    JobPriority priorityFor(ChunkPos pos) const;

    /// Runs the pipeline until nothing is outstanding. Used by --warm-up and by
    /// captures; never by the frame loop, which must not block.
    void drainStreaming();

    void startUploadThread();
    void shutdownStreaming();
    void uploadLoop();

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

    /// Job entry points. Static members rather than free functions so they can reach
    /// private state; `Job::Fn` is a plain function pointer either way.
    static void generateColumnJob(void* context, u64 payload);
    static void meshSectionJob(void* context, u64 payload);
    /// True when every column of `pos`'s 3x3 neighbourhood holds generated voxels.
    ///
    /// Meshing before that would cull the section's boundary faces against columns
    /// that are still empty, and the result would have to be thrown away. Waiting
    /// also removes the need to remesh on arrival entirely: a section is never
    /// meshed against a neighbour that is about to change.
    bool neighboursReady(ChunkPos pos) const;

    void buildVisibleSet();
    void renderFrame();
    void captureAndExit();
    void reportStats(f64 fps, f64 frameMs);
    void runBenchmark();
    /// Keeps the benchmark camera a fixed distance above the terrain below it.
    void followGround();

    /// Eye height above ground for the benchmark flight.
    static constexpr f32 kBenchEyeHeight = 14.0f;
    /// One iteration of the frame loop, minus windowing and input.
    void stepFrame(f64 deltaTime);

    Options m_options;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<Input> m_input;

    // Deferred: these issue GL calls on construction, so they cannot be direct
    // members -- members are initialized before the constructor body has had a
    // chance to create the device and load the GL entry points.
    std::optional<ChunkRenderer> m_chunkRenderer;
    std::optional<SectionMeshStore> m_meshStore;
    std::optional<CharacterRenderer> m_character;

    std::unique_ptr<World> m_world;
    std::unique_ptr<Generator> m_generator;

    Camera m_camera;
    /// Fly mode only. Walking speeds are fixed constants -- a player does not have
    /// a speed slider, and one that varies stops reading as walking.
    f32 m_moveSpeed = 24.0f;

    // Walking. Deliberately a very small amount of physics: gravity, a ground
    // height, and a maximum step. There is no collision volume and no horizontal
    // sweep -- a move is accepted or refused whole, by comparing the ground height
    // at the destination with the ground height here. That is enough to walk over
    // terrain and be stopped by a cliff, and it is not enough to stand on the side
    // of an overhang, which is a thing this engine cannot currently do.
    bool m_flying = false;
    bool m_onGround = false;
    f32 m_verticalVelocity = 0.0f;

    /// Minecraft's own: 4.317 walking, 5.612 sprinting, and a jump that clears one
    /// block. Reproduced rather than picked, because "feels like walking" is
    /// mostly this number being right.
    static constexpr f32 kWalkSpeed = 4.317f;
    static constexpr f32 kSprintSpeed = 5.612f;
    static constexpr f32 kSneakSpeed = 1.3f;
    static constexpr f32 kGravity = 28.0f;
    static constexpr f32 kJumpVelocity = 8.5f;
    static constexpr f32 kTerminalVelocity = 60.0f;
    /// One block plus a little, so terraced terrain is walked rather than jumped.
    static constexpr f32 kStepHeight = 1.05f;
    /// How far down to look for something to stand on before giving up.
    static constexpr i32 kGroundSearchDepth = 96;

    /// The camera the frame is actually drawn from.
    ///
    /// Equal to `m_camera` in first person. In third person it is pulled back along
    /// the view direction, and only this one moves -- `m_camera` stays the player's
    /// eye, because that is what streaming centres on and what the character is
    /// drawn from. Two cameras rather than one moving camera, so "where the player
    /// is" and "where the frame is seen from" cannot drift apart.
    Camera m_renderCamera;

    bool m_thirdPerson = false;
    /// Advances with distance travelled, not with time, so the limbs stop when the
    /// player does rather than marching on the spot.
    f32 m_walkPhase = 0.0f;
    f32 m_walkAmount = 0.0f;

    /// Frame counter, and the clock the mesh store's deferred reuse runs on.
    u64 m_frame = 0;

    ChunkPos m_loadedCenter{};
    bool m_hasLoadedCenter = false;

    /// In-flight meshing tasks. Sized once, before any thread starts, and never
    /// resized -- indices into it travel through queues.
    static constexpr usize kMeshTaskPoolSize = 1024;

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
    /// Meshing jobs completed. Deliberately separate from the mesh store's section
    /// count: a section entirely inside solid rock has every face hidden by its
    /// neighbours, so it is meshed, produces zero quads, and is stored nowhere. The
    /// gap between these two numbers is how much of the world costs nothing to draw.
    std::atomic<usize> m_sectionsMeshed{0};
    std::atomic<usize> m_sectionsEmpty{0};

    /// Declared after everything it touches, so it is joined before any of it is
    /// destroyed. shutdownStreaming() still signals it explicitly first, because a
    /// thread parked on a semaphore cannot observe a destructor.
    std::jthread m_uploadThread;

    f64 m_lastFrameTime = 0.0;
    f64 m_fpsAccumulator = 0.0;
    u32 m_framesSinceReport = 0;
    bool m_reportedWarm = false;
};

} // namespace mc
