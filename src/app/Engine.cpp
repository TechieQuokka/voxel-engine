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

    // Worth one line: FastNoise2 dispatches on the CPU at runtime, and the gap between
    // AVX2 and SSE2 is large enough that a generation timing is meaningless without
    // knowing which one ran.
    logInfo("Terrain noise: FastNoise2 on {}, density grid {}x{}x{} per column "
            "({} samples for {} voxels)",
            m_generator->graph().simdLevelName(),
            DensityField::kGridX, DensityField::kGridY, DensityField::kGridZ,
            DensityField::kSampleCount,
            static_cast<usize>(kSectionSize) * kSectionSize * static_cast<usize>(kWorldHeight));

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

    if (m_options.meshBenchmark) {
        runMeshBenchmark();
    }

    // Spawn above the actual ground rather than at a fixed height.
    //
    // A constant worked while terrain was a placeholder that never rose above y=60.
    // With a real density field the surface moves with continentalness -- 73 at the
    // origin, 91 a few hundred blocks away -- so a fixed camera height starts the
    // engine underground, and what that looks like on screen is not obviously a
    // spawn bug.
    constexpr f32 kEyeHeightAboveGround = 12.0f;
    const i32 groundY = m_generator->surfaceHeight(0, 0);
    m_camera.setPosition({0.0f, static_cast<f32>(groundY) + kEyeHeightAboveGround, 0.0f});
    m_camera.setOrientation(0.6f, -0.18f);
    updateProjection();

    logInfo("Spawn: ground at y={}, camera at y={:.0f}", groundY, m_camera.position().y);

    // The outermost loaded ring is never meshed -- neighboursReady() refuses it --
    // so the darkening has to bottom out before it, or the world would visibly end.
    const auto meshedBlocks =
        static_cast<f32>(std::max(1, m_options.renderDistance - 1) * kSectionSize);
    m_chunkRenderer->setFadeDistance(meshedBlocks);
    m_chunkRenderer->setFogColor(skyColorLinear());

    updateLoadedRegion();

    if (m_options.warmUp || !m_options.capturePath.empty()) {
        // Blocks on purpose, and only here: a capture should show a finished world
        // rather than whatever had streamed in by frame one, and a benchmark should
        // measure the steady state rather than the fill.
        Clock clock;
        drainStreaming();
        const f64 seconds = clock.elapsed();

        const usize meshed = m_sectionsMeshed.load();
        const usize empty = m_sectionsEmpty.load();
        logInfo("Warm-up: {} columns, {} sections meshed in {:.2f} s on {} workers",
                m_world->loadedChunkCount(), meshed, seconds, m_jobs->workerCount());
        logInfo("  {} sections hold geometry, {} are fully enclosed and hold none",
                m_meshStore->sectionCount(), empty);
        m_reportedWarm = true;
    }

    logInfo("Engine initialized (render distance {}, {} columns loaded)",
            m_options.renderDistance, m_world->loadedChunkCount());
}

Engine::~Engine() {
    shutdownStreaming();
}

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

void Engine::generateColumnJob(void* context, u64 payload) {
    MC_PROFILE_SCOPE_N("generateColumnJob");

    auto* generator = static_cast<Generator*>(context);
    auto* chunk = reinterpret_cast<Chunk*>(static_cast<std::uintptr_t>(payload));

    // Sets the column Ready and marks every section dirty when it finishes. The
    // Generating state it was put in before submission is what keeps the World from
    // unloading it while this runs.
    generator->generateColumn(*chunk);
}

void Engine::meshSectionJob(void* context, u64 payload) {
    MC_PROFILE_SCOPE_N("meshSectionJob");

    auto* engine = static_cast<Engine*>(context);
    const auto index = static_cast<usize>(payload);
    MeshTask& task = engine->m_meshTasks[index];

    meshSectionGreedy(task.hood, task.mesh);

    // The upload queue is at least as large as the task pool, so there is always
    // room -- a slot cannot be in flight without having come from the pool.
    const bool pushed = engine->m_uploadQueue->tryPush(static_cast<u32>(index));
    MC_VERIFY_MSG(pushed, "upload queue smaller than the mesh task pool");

    engine->m_uploadSignal.release();
}

JobPriority Engine::priorityFor(ChunkPos pos) const {
    const i32 distance = std::max(std::abs(pos.x - m_loadedCenter.x),
                                  std::abs(pos.z - m_loadedCenter.z));
    const i32 renderDistance = std::max(1, m_options.renderDistance);

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

usize Engine::submitGeneration() {
    MC_PROFILE_SCOPE_N("Engine::submitGeneration");

    usize submitted = 0;
    m_world->forEachChunk([&](Chunk& chunk) {
        if (submitted >= kColumnsPerFrame || chunk.state() != ChunkState::Empty) {
            return;
        }

        // Claimed before submitting: the World must not unload a column a worker is
        // about to write into, and Generating is what tells it so.
        chunk.setState(ChunkState::Generating);

        const Job job{&generateColumnJob, m_generator.get(),
                      static_cast<u64>(reinterpret_cast<std::uintptr_t>(&chunk))};

        if (!m_jobs->submit(priorityFor(chunk.position()), job)) {
            chunk.setState(ChunkState::Empty); // Band full; try again next frame.
            return;
        }
        ++submitted;
    });
    return submitted;
}

usize Engine::submitMeshing() {
    MC_PROFILE_SCOPE_N("Engine::submitMeshing");

    usize submitted = 0;

    m_world->forEachChunk([&](Chunk& chunk) {
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
                m_meshStore->release(pos, m_frame);
                chunk.clearSectionDirty(index);
                continue;
            }

            u32 taskIndex = 0;
            if (!m_freeMeshTasks->tryPop(taskIndex)) {
                return; // Pool exhausted. Backpressure, not an error.
            }

            MeshTask& task = m_meshTasks[taskIndex];
            task.pos = pos;
            task.hood = m_world->neighbourhood(pos);

            // Pin every column the neighbourhood points into, for as long as the
            // task lives -- which is until the upload thread is done with it, not
            // until the mesher returns.
            usize slot = 0;
            for (i32 dz = -1; dz <= 1; ++dz) {
                for (i32 dx = -1; dx <= 1; ++dx) {
                    Chunk* neighbour = m_world->find(ChunkPos{pos.x + dx, pos.z + dz});
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

void Engine::uploadLoop() {
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
        if (!m_meshStore->store(task.pos, task.mesh, frame)) {
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

void Engine::startUploadThread() {
    m_uploadThread = std::jthread([this] { uploadLoop(); });
}

void Engine::shutdownStreaming() {
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

void Engine::drainStreaming() {
    // Submit, let the pool finish, repeat. Generation produces meshing work, so this
    // needs more than one pass; it settles when a full round submits nothing.
    for (;;) {
        const usize generated = submitGeneration();
        const usize meshedSubmitted = submitMeshing();

        m_jobs->waitIdle();

        // Wait for the upload thread to drain what those jobs produced. Nothing else
        // is running by now, so an empty queue means finished.
        while (m_uploadQueue->sizeApprox() != 0) {
            std::this_thread::yield();
        }

        if (generated == 0 && meshedSubmitted == 0) {
            break;
        }
    }
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
    (void)deltaTime;

    // Return ranges retired long enough ago before allocating any new ones.
    m_meshStore->recycle(m_frame);

    updateLoadedRegion();

    // Submit only. Nothing here waits for a worker, which is the entire point:
    // whatever the pool has not finished simply appears a frame or two later.
    const usize generated = submitGeneration();
    const usize meshed = submitMeshing();

    if (!m_reportedWarm && generated == 0 && meshed == 0 && m_jobs->pending() == 0) {
        logInfo("Streaming settled: {} columns, {} sections meshed, {} MiB arena",
                m_world->loadedChunkCount(), m_meshStore->sectionCount(),
                m_meshStore->usedBytes() / (1024 * 1024));
        m_reportedWarm = true;
    }

    renderFrame();

    ++m_frame;
    m_frameForUpload.store(m_frame, std::memory_order_relaxed);
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
