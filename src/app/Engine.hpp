#pragma once

#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/ChunkRenderer.hpp"
#include "render/SectionMeshStore.hpp"
#include "rhi/Device.hpp"
#include "world/World.hpp"
#include "worldgen/Generator.hpp"

#include <memory>
#include <optional>
#include <string>
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

        /// Streams the whole region in before the first frame and logs how long it
        /// took, instead of filling in over several seconds. For measurement.
        bool warmUp = false;

        /// Runs this many frames with vsync off and no cursor capture, then reports
        /// the frame-time distribution and exits.
        ///
        /// Phase 3's exit criterion is a claim about frame time, and vsync makes
        /// that unmeasurable -- every frame reads as 16.7 ms whatever the real cost.
        /// The camera flies forward during the run so streaming is measured too; a
        /// static camera would report the frame time of a world that never changes.
        u32 benchFrames = 0;
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

    ChunkPos cameraColumn() const;

    /// Loads and unloads columns around the camera. Only does work when the camera
    /// has crossed into a different column.
    void updateLoadedRegion();
    /// Generates up to a budget of columns, nearest first.
    usize generatePending();
    /// Meshes up to a budget of dirty sections and uploads them.
    usize meshPending();
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

    std::unique_ptr<World> m_world;
    std::unique_ptr<Generator> m_generator;

    Camera m_camera;
    f32 m_moveSpeed = 24.0f;

    /// Frame counter, and the clock the mesh store's deferred reuse runs on.
    u64 m_frame = 0;

    ChunkPos m_loadedCenter{};
    bool m_hasLoadedCenter = false;

    /// Reused across frames so the streaming path does not allocate.
    ChunkMesh m_meshScratch;
    std::vector<ChunkPos> m_unloadedScratch;

    f64 m_lastFrameTime = 0.0;
    f64 m_fpsAccumulator = 0.0;
    u32 m_framesSinceReport = 0;
    bool m_reportedWarm = false;
};

} // namespace mc
