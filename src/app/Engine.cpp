#include "app/Engine.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "mesh/CulledMesher.hpp"
#include "platform/Clock.hpp"
#include "world/BlockRegistry.hpp"

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace mc {
namespace {

constexpr f64 kFpsReportInterval = 1.0;
constexpr f32 kMouseSensitivity = 0.0022f;

/// The test section sits at the world origin.
constexpr vec3 kSectionOrigin{0.0f, 0.0f, 0.0f};

/// Writes binary PPM (P6). Chosen over PNG because it needs no dependency and
/// every image tool reads it; capture output is a debugging artefact, not
/// something that has to be small.
void writePpm(const std::filesystem::path& path,
              int width,
              int height,
              const std::vector<u8>& rgba) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Cannot write capture to {}", path.string()));
    }

    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const char rgb[3] = {static_cast<char>(rgba[i]),
                             static_cast<char>(rgba[i + 1]),
                             static_cast<char>(rgba[i + 2])};
        file.write(rgb, sizeof(rgb));
    }

    if (!file) {
        throw std::runtime_error(std::format("Failed while writing {}", path.string()));
    }
}

} // namespace

Engine::Engine(Options options) : m_options(std::move(options)) {
    m_window = std::make_unique<Window>(Window::Config{
        .width = 1280,
        .height = 720,
        .title = "minecraft",
        .vsync = true,
        .debugContext = true,
    });

    m_device = std::make_unique<rhi::Device>(Window::glProcLoader());
    m_device->setViewport(0, 0, m_window->framebufferWidth(), m_window->framebufferHeight());
    m_device->setDepthTest(true);
    m_device->setBackfaceCulling(true);

    m_input = std::make_unique<Input>(*m_window);
    m_chunkRenderer.emplace();

    buildTestSection();

    ChunkMesh mesh;
    meshSectionCulled(m_section, mesh);
    m_chunkRenderer->upload(mesh);

    const usize faceCount = static_cast<usize>(kSectionVolume) * 6;
    logInfo("Section meshed: {} quads (of {} possible faces, {:.1f}% emitted)",
            mesh.quadCount(), faceCount,
            100.0 * static_cast<f64>(mesh.quadCount()) / static_cast<f64>(faceCount));
    logInfo("Section storage: {} bits/voxel, palette {} entries, {} bytes",
            m_section.storage().bitsPerIndex(),
            m_section.storage().paletteSize(),
            m_section.memoryUsage());

    // Outside the section, aimed at its centre.
    m_camera.setPosition({-24.0f, 34.0f, 56.0f});
    m_camera.setOrientation(0.785f, -0.43f);
    const f32 aspect = static_cast<f32>(m_window->framebufferWidth())
                     / static_cast<f32>(m_window->framebufferHeight());
    m_camera.setPerspective(math::radians(70.0f), aspect, 0.05f);

    logInfo("Engine initialized");
}

Engine::~Engine() = default;

void Engine::buildTestSection() {
    MC_PROFILE_SCOPE_N("buildTestSection");

    // Placeholder terrain: a rolling surface plus a solid base. Real generation
    // arrives in Phase 4; the point here is to produce a section whose palette
    // holds several block types and whose surface exercises face culling on all
    // six directions.
    for (i32 z = 0; z < kSectionSize; ++z) {
        for (i32 x = 0; x < kSectionSize; ++x) {
            const f32 fx = static_cast<f32>(x);
            const f32 fz = static_cast<f32>(z);
            const f32 wave = 8.0f
                           + 4.0f * std::sin(fx * 0.28f)
                           + 3.0f * std::cos(fz * 0.21f)
                           + 2.0f * std::sin((fx + fz) * 0.15f);
            const i32 height = math::clamp(static_cast<i32>(wave), 1, kSectionSize - 1);

            for (i32 y = 0; y <= height; ++y) {
                BlockId block = kStoneBlock;
                if (y == height) {
                    block = kGrassBlock;
                } else if (y > height - 4) {
                    block = kDirtBlock;
                }
                m_section.set(x, y, z, block);
            }
        }
    }

    // A sand pillar, so that vertical side faces and an overhanging top are
    // both present in the mesh.
    for (i32 y = 0; y < 26; ++y) {
        m_section.set(16, y, 16, kSandBlock);
        m_section.set(17, y, 16, kSandBlock);
        m_section.set(16, y, 17, kSandBlock);
        m_section.set(17, y, 17, kSandBlock);
    }
}

void Engine::updateCamera(f64 deltaTime) {
    const f32 dt = static_cast<f32>(deltaTime);

    if (m_input->wasPressed(Key::Escape)) {
        if (m_input->cursorCaptured()) {
            m_input->setCursorCaptured(false);
        } else {
            m_window->requestClose();
        }
    }

    if (m_input->cursorCaptured()) {
        m_camera.rotate(static_cast<f32>(m_input->mouseDeltaX()) * kMouseSensitivity,
                        static_cast<f32>(-m_input->mouseDeltaY()) * kMouseSensitivity);
    }

    vec3 delta{0.0f};
    if (m_input->isDown(Key::W)) { delta += m_camera.forward(); }
    if (m_input->isDown(Key::S)) { delta -= m_camera.forward(); }
    if (m_input->isDown(Key::D)) { delta += m_camera.right(); }
    if (m_input->isDown(Key::A)) { delta -= m_camera.right(); }
    if (m_input->isDown(Key::Space)) { delta += Camera::up(); }
    if (m_input->isDown(Key::LeftShift)) { delta -= Camera::up(); }

    if (math::dot(delta, delta) > 0.0f) {
        const f32 speed = m_input->isDown(Key::LeftControl) ? m_moveSpeed * 4.0f : m_moveSpeed;
        m_camera.move(math::normalize(delta) * speed * dt);
    }
}

void Engine::captureAndExit() {
    const int width = m_window->framebufferWidth();
    const int height = m_window->framebufferHeight();

    renderFrame();
    const std::vector<u8> pixels = m_device->readFramebufferRgba(width, height);
    writePpm(m_options.capturePath, width, height, pixels);

    logInfo("Captured {}x{} frame to {}", width, height, m_options.capturePath);
}

void Engine::run() {
    MC_PROFILE_THREAD("main");

    if (!m_options.capturePath.empty()) {
        captureAndExit();
        return;
    }

    m_input->setCursorCaptured(true);

    Clock clock;
    m_lastFrameTime = clock.elapsed();

    while (!m_window->shouldClose()) {
        MC_PROFILE_SCOPE_N("frame");

        const f64 now = clock.elapsed();
        const f64 deltaTime = now - m_lastFrameTime;
        m_lastFrameTime = now;

        m_window->pollEvents();
        m_input->update();

        if (m_window->consumeResizeEvent()) {
            const int width = m_window->framebufferWidth();
            const int height = m_window->framebufferHeight();
            m_device->setViewport(0, 0, width, height);
            if (height > 0) {
                m_camera.setPerspective(math::radians(70.0f),
                                        static_cast<f32>(width) / static_cast<f32>(height),
                                        0.05f);
            }
        }

        updateCamera(deltaTime);
        renderFrame();
        m_window->swapBuffers();

        MC_PROFILE_FRAME();

        m_fpsAccumulator += deltaTime;
        ++m_framesSinceReport;
        if (m_fpsAccumulator >= kFpsReportInterval) {
            const f64 fps = static_cast<f64>(m_framesSinceReport) / m_fpsAccumulator;
            const f64 frameMs = 1000.0 * m_fpsAccumulator / static_cast<f64>(m_framesSinceReport);
            logInfo("{:.1f} FPS ({:.2f} ms/frame)", fps, frameMs);
            m_fpsAccumulator = 0.0;
            m_framesSinceReport = 0;
        }
    }
}

void Engine::renderFrame() {
    MC_PROFILE_SCOPE_N("renderFrame");

    m_device->clear(0.53f, 0.71f, 0.92f, 1.0f);
    m_chunkRenderer->draw(*m_device, m_camera, kSectionOrigin);
}

} // namespace mc
