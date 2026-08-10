#pragma once

#include "core/Types.hpp"
#include "mesh/ChunkMesh.hpp"
#include "platform/Input.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/ChunkRenderer.hpp"
#include "rhi/Device.hpp"
#include "world/Section.hpp"

#include <memory>
#include <optional>
#include <string>

namespace mc {

/// Owns the window, the graphics device, and the frame loop.
///
/// Phase 1 renders a single meshed section with a free-flying camera. World
/// streaming and multi-chunk rendering arrive in Phase 3.
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
    };

    /// No default argument: a nested struct's default member initializers are
    /// parsed only after the enclosing class is complete, so `= {}` here cannot
    /// see them. main always constructs an Options anyway.
    explicit Engine(Options options);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    void buildTestSection();
    /// Meshes the test section with each strategy, logs the comparison, and
    /// returns the mesh to actually render. Only runs under
    /// Options::meshBenchmark.
    ChunkMesh reportMeshingComparison();
    /// Rebuilds the projection from the current framebuffer size. Called at
    /// startup and on every resize, so the two cannot drift apart.
    void updateProjection();
    void updateCamera(f64 deltaTime);
    void renderFrame();
    void captureAndExit();

    Options m_options;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<Input> m_input;

    // Deferred: these issue GL calls on construction, so they cannot be direct
    // members -- members are initialized before the constructor body has had a
    // chance to create the device and load the GL entry points.
    std::optional<ChunkRenderer> m_chunkRenderer;

    Section m_section;
    Camera m_camera;
    f32 m_moveSpeed = 12.0f;

    f64 m_lastFrameTime = 0.0;
    f64 m_fpsAccumulator = 0.0;
    u32 m_framesSinceReport = 0;
};

} // namespace mc
