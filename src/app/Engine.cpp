#include "app/Engine.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Profile.hpp"
#include "mesh/BinaryGreedyMesher.hpp"
#include "mesh/CulledMesher.hpp"
#include "platform/Clock.hpp"
#include "render/Frustum.hpp"
#include "world/BlockRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mc {
namespace {

constexpr f64 kFpsReportInterval = 1.0;
constexpr f32 kMouseSensitivity = 0.0022f;

constexpr f32 kFovYDegrees = 70.0f;

/// Reversed-Z concentrates depth precision in the distance, which is what allows
/// a near plane this close without z-fighting far away.
constexpr f32 kNearPlane = 0.05f;

/// Per-frame streaming budgets, for the single-threaded path.
///
/// Generation is ~0.40 ms per column and meshing ~0.12 ms per section, so these are
/// roughly 6 ms and 4 ms of work -- enough to fill a distance-16 region in about a
/// second, and too much to hold 60 FPS while doing it. That is the honest state of
/// 3d: the budgets exist so the frame loop stays responsive, and 3f removes the
/// need for them by moving both onto the worker pool.
constexpr usize kColumnsPerFrame = 16;
constexpr usize kSectionsPerFrame = 32;

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
    m_meshStore.emplace(meshArenaBytesFor(m_options.renderDistance));

    m_world = std::make_unique<World>(m_options.renderDistance);
    m_generator = std::make_unique<Generator>();

    if (m_options.meshBenchmark) {
        runMeshBenchmark();
    }

    // Start above the terrain, looking slightly down.
    m_camera.setPosition({0.0f, 70.0f, 0.0f});
    m_camera.setOrientation(0.6f, -0.25f);
    updateProjection();

    // The outermost loaded ring is never meshed -- neighboursReady() refuses it --
    // so the darkening has to bottom out before it, or the world would visibly end.
    const auto meshedBlocks =
        static_cast<f32>(std::max(1, m_options.renderDistance - 1) * kSectionSize);
    m_chunkRenderer->setFadeDistance(meshedBlocks);
    m_chunkRenderer->setFogColor(skyColorLinear());

    updateLoadedRegion();

    if (m_options.warmUp || !m_options.capturePath.empty()) {
        Clock clock;
        usize columns = 0;
        usize sections = 0;
        // Unbudgeted: this path exists to measure the total, and to make a capture
        // show a finished world rather than whatever streamed in by frame one.
        for (;;) {
            const usize generated = generatePending();
            const usize meshed = meshPending();
            columns += generated;
            sections += meshed;
            if (generated == 0 && meshed == 0) {
                break;
            }
        }
        logInfo("Warm-up: {} columns generated, {} sections meshed in {:.2f} s",
                columns, sections, clock.elapsed());
        m_reportedWarm = true;
    }

    logInfo("Engine initialized (render distance {}, {} columns loaded)",
            m_options.renderDistance, m_world->loadedChunkCount());
}

Engine::~Engine() = default;

void Engine::runMeshBenchmark() {
    // The Phase 2 exit criterion is a measured quad-count reduction, and the
    // open design question is what AO-aware merging actually costs. Both are
    // answered here rather than assumed.
    constexpr int kTimingRuns = 200;

    // A rolling surface plus a solid base, with a sand pillar so that vertical side
    // faces and an overhanging top are both present.
    Section section;
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
                section.set(x, y, z, block);
            }
        }
    }
    for (i32 y = 0; y < 26; ++y) {
        section.set(16, y, 16, kSandBlock);
        section.set(17, y, 16, kSandBlock);
        section.set(16, y, 17, kSandBlock);
        section.set(17, y, 17, kSandBlock);
    }

    struct Variant {
        const char* name;
        ChunkMesh mesh;
        f64 microseconds = 0.0;
    };

    ChunkMesh culled;
    meshSectionCulled(section, culled);

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
        meshSectionCulled(section, scratch);
    }
    const f64 culledMicros =
        (clock.elapsed() - culledStart) * 1e6 / static_cast<f64>(kTimingRuns);

    for (usize i = 0; i < variants.size(); ++i) {
        meshSectionGreedy(section, variants[i].mesh, configs[i]);

        const f64 start = clock.elapsed();
        for (int run = 0; run < kTimingRuns; ++run) {
            ChunkMesh scratch;
            meshSectionGreedy(section, scratch, configs[i]);
        }
        variants[i].microseconds =
            (clock.elapsed() - start) * 1e6 / static_cast<f64>(kTimingRuns);
    }

    const auto baseline = static_cast<f64>(culled.quadCount());

    logInfo("--- meshing comparison (32^3 test section, isolated) ---");
    logInfo("{:<24} {:>8} {:>10} {:>12}", "strategy", "quads", "reduction", "time");
    logInfo("{:<24} {:>8} {:>10} {:>10.1f}us", "culled (reference)",
            culled.quadCount(), "-", culledMicros);

    for (const Variant& variant : variants) {
        const f64 reduction =
            100.0 * (1.0 - static_cast<f64>(variant.mesh.quadCount()) / baseline);
        logInfo("{:<24} {:>8} {:>9.1f}% {:>10.1f}us",
                variant.name, variant.mesh.quadCount(), reduction, variant.microseconds);
    }
    logInfo("--------------------------------------------------------");
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

ChunkPos Engine::cameraColumn() const {
    const vec3& position = m_camera.position();
    return toChunkPos(BlockPos{static_cast<i32>(std::floor(position.x)),
                               static_cast<i32>(std::floor(position.y)),
                               static_cast<i32>(std::floor(position.z))});
}

void Engine::updateLoadedRegion() {
    MC_PROFILE_SCOPE_N("Engine::updateLoadedRegion");

    const ChunkPos center = cameraColumn();
    if (m_hasLoadedCenter && center == m_loadedCenter) {
        return; // Still in the same column; the loaded set cannot have changed.
    }

    World::LoadResult result = m_world->updateLoadedRegion(center);
    m_loadedCenter = center;
    m_hasLoadedCenter = true;

    for (const ChunkPos pos : result.unloadedPositions) {
        // Give back the GPU storage.
        for (i32 sectionY = kMinSectionY; sectionY < kMaxSectionY; ++sectionY) {
            m_meshStore->release(SectionPos{pos.x, sectionY, pos.z}, m_frame);
        }

        // The survivors next door had their boundary faces culled against this
        // column. With it gone, those faces meet air and have to be emitted, so
        // whatever is still loaded around it needs remeshing.
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    continue;
                }
                if (Chunk* neighbour = m_world->find(ChunkPos{pos.x + dx, pos.z + dz})) {
                    neighbour->markAllDirty();
                }
            }
        }
    }

    if (result.created > 0 || result.unloaded > 0) {
        logDebug("Region {},{}: +{} -{} (retained {}), {} loaded",
                 center.x, center.z, result.created, result.unloaded, result.retained,
                 m_world->loadedChunkCount());
    }
}

usize Engine::generatePending() {
    MC_PROFILE_SCOPE_N("Engine::generatePending");

    const usize budget = m_options.warmUp || !m_options.capturePath.empty()
                             ? ~usize{0}
                             : kColumnsPerFrame;

    usize generated = 0;
    m_world->forEachChunk([&](Chunk& chunk) {
        if (generated >= budget || chunk.state() != ChunkState::Empty) {
            return;
        }
        m_generator->generateColumn(chunk); // Sets Ready and marks every section dirty.
        ++generated;
    });
    return generated;
}

bool Engine::neighboursReady(ChunkPos pos) const {
    for (i32 dz = -1; dz <= 1; ++dz) {
        for (i32 dx = -1; dx <= 1; ++dx) {
            const Chunk* chunk = m_world->find(ChunkPos{pos.x + dx, pos.z + dz});
            if (chunk == nullptr || chunk->state() != ChunkState::Ready) {
                return false;
            }
        }
    }
    return true;
}

usize Engine::meshPending() {
    MC_PROFILE_SCOPE_N("Engine::meshPending");

    const usize budget = m_options.warmUp || !m_options.capturePath.empty()
                             ? ~usize{0}
                             : kSectionsPerFrame;

    usize meshed = 0;
    bool arenaFull = false;

    m_world->forEachChunk([&](Chunk& chunk) {
        if (meshed >= budget || arenaFull || !chunk.anyDirty()) {
            return;
        }
        if (!neighboursReady(chunk.position())) {
            return;
        }

        for (usize index = 0; index < Chunk::kSectionCount && meshed < budget; ++index) {
            if (!chunk.isSectionDirty(index)) {
                continue;
            }

            const SectionPos pos{chunk.position().x,
                                 kMinSectionY + static_cast<i32>(index),
                                 chunk.position().z};

            const Section& section = chunk.sectionByIndex(index);
            if (section.isEmpty()) {
                // Nothing to draw, and nothing to keep: an all-air section is the
                // common case above the surface.
                m_meshStore->release(pos, m_frame);
                chunk.clearSectionDirty(index);
                continue;
            }

            meshSectionGreedy(m_world->neighbourhood(pos), m_meshScratch);

            if (!m_meshStore->store(pos, m_meshScratch, m_frame)) {
                // Arena full. Leave the section dirty and try again next frame,
                // once recycle() has returned the retired ranges.
                arenaFull = true;
                return;
            }

            chunk.clearSectionDirty(index);
            ++meshed;
        }
    });

    if (arenaFull) {
        logWarn("Mesh arena full: {} / {} MiB used, largest free block {} KiB",
                m_meshStore->usedBytes() / (1024 * 1024),
                m_meshStore->capacityBytes() / (1024 * 1024),
                m_meshStore->largestFreeBlock() / 1024);
    }

    return meshed;
}

void Engine::buildVisibleSet() {
    MC_PROFILE_SCOPE_N("Engine::buildVisibleSet");

    const Frustum frustum(m_camera.viewProjectionMatrix());

    m_chunkRenderer->beginFrame();
    ChunkRenderer::Stats& stats = m_chunkRenderer->stats();

    vec3 minCorner{0.0f};
    vec3 maxCorner{0.0f};

    m_world->forEachChunk([&](const Chunk& chunk) {
        // Column first: one test against a 32x384x32 box rejects twelve section
        // tests, and most columns at distance 16 are outside the frustum.
        columnBounds(chunk.position(), minCorner, maxCorner);
        if (!frustum.intersectsAabb(minCorner, maxCorner)) {
            ++stats.columnsCulled;
            return;
        }

        for (i32 sectionY = kMinSectionY; sectionY < kMaxSectionY; ++sectionY) {
            const SectionPos pos{chunk.position().x, sectionY, chunk.position().z};
            ++stats.sectionsConsidered;

            sectionBounds(pos, minCorner, maxCorner);
            if (!frustum.intersectsAabb(minCorner, maxCorner)) {
                ++stats.sectionsCulled;
                continue;
            }

            // Looked up only after the frustum test: find() takes the store's mutex,
            // and doing it for all ~13,000 sections rather than the visible few
            // would cost more than the culling saves. Phase 5 moves this list to the
            // GPU and the question disappears.
            if (const auto placement = m_meshStore->find(pos)) {
                m_chunkRenderer->addSection(pos, *placement);
            }
        }
    });
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

    const ChunkRenderer::Stats& stats = m_chunkRenderer->stats();
    logInfo("Captured {}x{} frame to {} ({} sections, {} quads drawn)",
            width, height, m_options.capturePath, stats.sectionsDrawn, stats.quadsDrawn);
}

void Engine::reportStats(f64 fps, f64 frameMs) {
    const ChunkRenderer::Stats& stats = m_chunkRenderer->stats();

    logInfo("{:.1f} FPS ({:.2f} ms) | {} cols | drawn {} sec / {} quads | "
            "culled {} col + {} sec | arena {} MiB",
            fps, frameMs,
            m_world->loadedChunkCount(),
            stats.sectionsDrawn, stats.quadsDrawn,
            stats.columnsCulled, stats.sectionsCulled,
            m_meshStore->usedBytes() / (1024 * 1024));
}

void Engine::stepFrame(f64 deltaTime) {
    // Return ranges retired long enough ago before allocating any new ones.
    m_meshStore->recycle(m_frame);

    updateLoadedRegion();
    const usize generated = generatePending();
    const usize meshed = meshPending();

    if (!m_reportedWarm && generated == 0 && meshed == 0) {
        logInfo("Streaming settled: {} columns, {} sections meshed, {} MiB arena",
                m_world->loadedChunkCount(), m_meshStore->sectionCount(),
                m_meshStore->usedBytes() / (1024 * 1024));
        m_reportedWarm = true;
    }

    (void)deltaTime;
    renderFrame();
    ++m_frame;
}

void Engine::runBenchmark() {
    m_window->setVsync(false);

    // Fly forward at a steady rate, so columns cross the region boundary and the
    // streaming path is part of what gets measured.
    constexpr f32 kFlySpeed = 40.0f;
    constexpr f64 kFixedStep = 1.0 / 60.0;

    std::vector<f64> frameMs;
    frameMs.reserve(m_options.benchFrames);

    Clock clock;
    f64 previous = clock.elapsed();

    for (u32 frame = 0; frame < m_options.benchFrames && !m_window->shouldClose(); ++frame) {
        m_window->pollEvents();

        // A fixed step rather than the measured one, so the camera follows the same
        // path regardless of how fast the machine runs it and two runs are
        // comparable.
        m_camera.move(m_camera.forward() * kFlySpeed * static_cast<f32>(kFixedStep));

        stepFrame(kFixedStep);
        m_window->swapBuffers();

        const f64 now = clock.elapsed();
        frameMs.push_back((now - previous) * 1000.0);
        previous = now;
    }

    if (frameMs.empty()) {
        logWarn("Benchmark produced no frames");
        return;
    }

    std::vector<f64> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());

    f64 total = 0.0;
    for (const f64 value : frameMs) {
        total += value;
    }

    const auto percentile = [&sorted](f64 fraction) {
        const auto index = static_cast<usize>(fraction * static_cast<f64>(sorted.size() - 1));
        return sorted[index];
    };

    const f64 mean = total / static_cast<f64>(frameMs.size());
    logInfo("--- {} frames, vsync off, render distance {} ---",
            frameMs.size(), m_options.renderDistance);
    logInfo("mean {:.2f} ms ({:.0f} FPS) | median {:.2f} | p99 {:.2f} | min {:.2f} | max {:.2f}",
            mean, 1000.0 / mean, percentile(0.5), percentile(0.99), sorted.front(), sorted.back());

    const ChunkRenderer::Stats& stats = m_chunkRenderer->stats();
    logInfo("last frame: {} sections drawn, {} quads, {} columns + {} sections culled",
            stats.sectionsDrawn, stats.quadsDrawn, stats.columnsCulled, stats.sectionsCulled);
    logInfo("arena: {} MiB of {} used across {} sections",
            m_meshStore->usedBytes() / (1024 * 1024),
            m_meshStore->capacityBytes() / (1024 * 1024),
            m_meshStore->sectionCount());
    logInfo("-------------------------------------------------");
}

void Engine::run() {
    MC_PROFILE_THREAD("main");

    if (!m_options.capturePath.empty()) {
        captureAndExit();
        return;
    }

    if (m_options.benchFrames > 0) {
        runBenchmark();
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
        stepFrame(deltaTime);
        m_window->swapBuffers();

        MC_PROFILE_FRAME();

        m_fpsAccumulator += deltaTime;
        ++m_framesSinceReport;
        if (m_fpsAccumulator >= kFpsReportInterval) {
            const f64 fps = static_cast<f64>(m_framesSinceReport) / m_fpsAccumulator;
            const f64 frameMs = 1000.0 * m_fpsAccumulator / static_cast<f64>(m_framesSinceReport);
            reportStats(fps, frameMs);
            m_fpsAccumulator = 0.0;
            m_framesSinceReport = 0;
        }
    }
}

void Engine::renderFrame() {
    MC_PROFILE_SCOPE_N("renderFrame");

    const vec3& sky = skyColorLinear();
    m_device->clear(sky.x, sky.y, sky.z, 1.0f);

    buildVisibleSet();
    m_chunkRenderer->draw(*m_device, m_camera, *m_meshStore);
}

} // namespace mc
