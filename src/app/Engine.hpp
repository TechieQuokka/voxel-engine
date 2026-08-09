#pragma once

#include "core/Types.hpp"
#include "platform/Window.hpp"
#include "rhi/Device.hpp"
#include "rhi/Shader.hpp"
#include "rhi/VertexArray.hpp"

#include <memory>
#include <optional>
#include <string>

namespace mc {

/// Owns the window, the graphics device, and the frame loop.
///
/// Phase 0 draws a single triangle to prove the toolchain, context creation,
/// and shader pipeline all work end to end. The rendering here moves into
/// `mc_render` in Phase 1.
class Engine {
public:
    struct Options {
        /// When set, renders a single frame, writes it to this path as a PNM
        /// image, and exits. Lets rendering be verified without a compositor
        /// screenshot, which is also how reference captures get taken in later
        /// phases.
        std::string capturePath;
    };

    explicit Engine(Options options = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    void renderFrame();
    void captureAndExit();

    Options m_options;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<rhi::Device> m_device;

    rhi::Shader m_triangleShader;

    // Deferred: constructing a VAO issues a GL call, so it cannot be a direct
    // member -- members are initialized before the constructor body has had a
    // chance to create the device and load the GL entry points.
    std::optional<rhi::VertexArray> m_emptyVao;

    f64 m_lastFrameTime = 0.0;
    f64 m_fpsAccumulator = 0.0;
    u32 m_framesSinceReport = 0;
};

} // namespace mc
