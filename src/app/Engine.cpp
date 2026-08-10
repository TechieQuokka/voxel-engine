#include "app/Engine.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "mesh/BinaryGreedyMesher.hpp"
#include "mesh/CulledMesher.hpp"
#include "platform/Clock.hpp"
#include "world/BlockRegistry.hpp"

#include <array>
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

constexpr f32 kFovYDegrees = 70.0f;

/// Reversed-Z concentrates depth precision in the distance, which is what allows
/// a near plane this close without z-fighting far away.
constexpr f32 kNearPlane = 0.05f;

/// Sky colour, written as the sRGB value it was picked as. The framebuffer is
/// sRGB-encoded on write, so Device::clear takes linear values -- decoded once
/// here rather than per frame.
const vec3& skyColorLinear() {
    static const vec3 color{rhi::srgbToLinear(0.53f),
                            rhi::srgbToLinear(0.71f),
                            rhi::srgbToLinear(0.92f)};
    return color;
}

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
    if (m_options.meshBenchmark) {
        mesh = reportMeshingComparison();
    } else {
        meshSectionGreedy(m_section, mesh);
    }
    m_chunkRenderer->upload(mesh);

    logInfo("Section storage: {} bits/voxel, palette {} entries, {} bytes",
            m_section.storage().bitsPerIndex(),
            m_section.storage().paletteSize(),
            m_section.memoryUsage());

    // Outside the section, aimed at its centre.
    m_camera.setPosition({-24.0f, 34.0f, 56.0f});
    m_camera.setOrientation(0.785f, -0.43f);
    updateProjection();

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

ChunkMesh Engine::reportMeshingComparison() {
    // The Phase 2 exit criterion is a measured quad-count reduction, and the
    // open design question is what AO-aware merging actually costs. Both are
    // answered here rather than assumed.
    constexpr int kTimingRuns = 200;

    struct Variant {
        const char* name;
        ChunkMesh mesh;
        f64 microseconds = 0.0;
    };

    ChunkMesh culled;
    meshSectionCulled(m_section, culled);

    std::array<Variant, 2> variants{{
        {"greedy + AO-aware merge", {}, 0.0},
        {"greedy, AO ignored", {}, 0.0},
    }};

    const std::array<GreedyMeshOptions, 2> configs{{
        {.ambientOcclusion = true, .aoAwareMerging = true},
        {.ambientOcclusion = true, .aoAwareMerging = false},
    }};

    Clock clock;

    const f64 culledStart = clock.elapsed();
    for (int i = 0; i < kTimingRuns; ++i) {
        ChunkMesh scratch;
        meshSectionCulled(m_section, scratch);
    }
    const f64 culledMicros =
        (clock.elapsed() - culledStart) * 1e6 / static_cast<f64>(kTimingRuns);

    for (usize i = 0; i < variants.size(); ++i) {
        meshSectionGreedy(m_section, variants[i].mesh, configs[i]);

        const f64 start = clock.elapsed();
        for (int run = 0; run < kTimingRuns; ++run) {
            ChunkMesh scratch;
            meshSectionGreedy(m_section, scratch, configs[i]);
        }
        variants[i].microseconds =
            (clock.elapsed() - start) * 1e6 / static_cast<f64>(kTimingRuns);
    }

    const auto baseline = static_cast<f64>(culled.quadCount());

    logInfo("--- meshing comparison (32^3 test section) ---");
    logInfo("{:<24} {:>8} {:>10} {:>12}", "strategy", "quads", "reduction", "time");
    logInfo("{:<24} {:>8} {:>10} {:>10.1f}us", "culled (reference)",
            culled.quadCount(), "-", culledMicros);

    for (const Variant& variant : variants) {
        const f64 reduction =
            100.0 * (1.0 - static_cast<f64>(variant.mesh.quadCount()) / baseline);
        logInfo("{:<24} {:>8} {:>9.1f}% {:>10.1f}us",
                variant.name, variant.mesh.quadCount(), reduction, variant.microseconds);
    }
    logInfo("----------------------------------------------");

    return std::move(variants[0].mesh);
}

void Engine::updateProjection() {
    const int width = m_window->framebufferWidth();
    const int height = m_window->framebufferHeight();
    if (width <= 0 || height <= 0) {
        return; // Minimized. The resize event that restores it will call again.
    }

    m_camera.setPerspective(math::radians(kFovYDegrees),
                            static_cast<f32>(width) / static_cast<f32>(height),
                            kNearPlane);
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
            m_device->setViewport(0, 0,
                                  m_window->framebufferWidth(),
                                  m_window->framebufferHeight());
            updateProjection();
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

    const vec3& sky = skyColorLinear();
    m_device->clear(sky.x, sky.y, sky.z, 1.0f);
    m_chunkRenderer->draw(*m_device, m_camera, kSectionOrigin);
}

} // namespace mc
