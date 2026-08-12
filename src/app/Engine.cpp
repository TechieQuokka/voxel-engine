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
    m_thirdPerson = m_options.thirdPerson;
    m_flying = m_options.flying;

    if (m_options.openInventory) {
        m_inventoryOpen = true;
        // Something to look at. A capture of an empty grid proves the panel draws
        // and nothing else -- not the icons, not the counts, not the two-digit
        // layout, which are the parts with arithmetic in them.
        m_inventory.add(blockIdOf("stone"), 64);
        m_inventory.add(blockIdOf("dirt"), 7);
        m_inventory.add(blockIdOf("grass"), 12);
        m_inventory.add(blockIdOf("sand"), 3);
        m_inventory.add(blockIdOf("oak_log"), 128);
        m_inventory.add(blockIdOf("cobblestone"), 45);
        m_inventory.add(blockIdOf("gravel"), 1);
        m_health = 13.0f; // An odd number, so a half heart is in the frame too.
    }
    m_window = std::make_unique<Window>(Window::Config{
        .width = 1280,
        .height = 720,
        .title = "minecraft",
        .vsync = true,
        .debugContext = true,
        .fullscreen = m_options.fullscreen,
    });

    m_device = std::make_unique<rhi::Device>(Window::glProcLoader());
    m_device->setViewport(0, 0, m_window->framebufferWidth(), m_window->framebufferHeight());
    m_device->setDepthTest(true);
    m_device->setBackfaceCulling(true);

    m_input = std::make_unique<Input>(*m_window);
    m_chunkRenderer.emplace();
    m_character.emplace();
    m_selection.emplace();
    m_itemRenderer.emplace();
    m_hud.emplace();
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
    const i32 groundY = m_generator->surfaceHeight(0, 0);
    // Feet on the block above the surface, eye at head height -- so the first frame
    // is what standing there looks like, not what hovering above it does.
    m_camera.setPosition({0.5f,
                          static_cast<f32>(groundY + 1) + CharacterRenderer::kEyeHeight,
                          0.5f});
    m_camera.setOrientation(0.6f, -0.18f);
    m_onGround = true;
    updateProjection();

    logInfo("Spawn: ground at y={}, standing with eye at y={:.2f}", groundY,
            m_camera.position().y);

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
            m_meshStore->usedBytes() / (1024 * 1024),
            m_meshStore->capacityBytes() / (1024 * 1024),
            m_meshStore->largestFreeBlock() / 1024,
            m_arenaFullEvents.load());
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

    const Frustum frustum(m_renderCamera.viewProjectionMatrix());

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
        if (m_inventoryOpen) {
            // Escape closes the window before it does anything else, which is what
            // every game does and what stops the reflex to back out of a menu from
            // quitting instead.
            toggleInventory();
        } else if (m_input->cursorCaptured()) {
            m_input->setCursorCaptured(false);
        } else {
            m_window->requestClose();
        }
    }

    if (m_input->wasPressed(Key::F5)) {
        m_thirdPerson = !m_thirdPerson;
        logInfo("Camera: {} person", m_thirdPerson ? "third" : "first");
    }

    // Nothing else to do: the window switches to the monitor's native mode and the
    // resize path already running in `renderFrame` picks up the new framebuffer, so
    // the viewport, the projection aspect and the HUD layout all follow on their own.
    if (m_input->wasPressed(Key::F11)) {
        m_window->toggleFullscreen();
    }

    if (m_input->cursorCaptured()) {
        m_camera.rotate(static_cast<f32>(m_input->mouseDeltaX()) * kMouseSensitivity,
                        static_cast<f32>(-m_input->mouseDeltaY()) * kMouseSensitivity);
    }

    if (m_input->wasPressed(Key::F)) {
        m_flying = !m_flying;
        m_verticalVelocity = 0.0f;
        logInfo("Movement: {}", m_flying ? "flying" : "walking");
    }

    const vec3 startPosition = m_camera.position();

    if (m_flying) {
        updateFly(dt);
    } else {
        updateWalk(dt);
    }

    // Drive the walk cycle from how far the player actually went, so the limbs are
    // in step with the ground rather than with the clock -- which also means they
    // stop dead when the player does.
    vec3 travelled = m_camera.position() - startPosition;
    travelled.y = 0.0f;
    const f32 distance = math::length(travelled);

    constexpr f32 kRadiansPerBlock = 2.4f;
    m_walkPhase += distance * kRadiansPerBlock;

    const f32 target = distance > 1e-4f ? 1.0f : 0.0f;
    m_walkAmount += (target - m_walkAmount) * std::min(1.0f, dt * 12.0f);
}

bool Engine::applyEdit(BlockPos pos, BlockId block) {
    switch (m_world->setBlock(pos, block)) {
        case World::EditStatus::Applied:
        case World::EditStatus::Unchanged:
            // Tell the neighbours. Digging the block out from under a sand pillar is
            // the only thing that makes it fall, and this is where the engine learns
            // that anything happened at all.
            m_blockUpdates.notify(pos);
            return true;

        case World::EditStatus::Busy:
            // A meshing job is reading this column. Not a failure -- come back next
            // frame, when the pin will almost certainly be gone.
            m_pendingEdits.push_back(PendingEdit{pos, block, 0});
            return true;

        case World::EditStatus::OutsideWorld:
        case World::EditStatus::NotLoaded:
            return false;
    }
    return false;
}

void Engine::breakTargetBlock() {
    if (!m_target.has_value()) {
        return;
    }

    // Read what was there *before* the edit: after it the block is air, and air
    // drops nothing.
    const BlockId broken = m_world->blockAt(m_target->block);

    if (!applyEdit(m_target->block, kAirBlock)) {
        return; // Not loaded, or outside the world. Nothing was removed, so nothing drops.
    }
    ++m_blocksBroken;

    const BlockId drop = dropOf(broken);
    if (drop == kAirBlock) {
        return; // Leaves, and anything else that yields nothing.
    }

    // At the centre of the block that was removed, which is where the space now is.
    m_items.spawn(vec3{static_cast<f32>(m_target->block.x) + 0.5f,
                       static_cast<f32>(m_target->block.y) + 0.5f,
                       static_cast<f32>(m_target->block.z) + 0.5f},
                  drop, 1);
}

void Engine::updateSwing(f32 dt, bool swinging) {
    constexpr f32 kTwoPi = 6.2831853f;

    if (swinging) {
        m_swingPhase += dt * kTwoPi / kSwingPeriod;
        // Wrapped rather than left to grow: at 60 FPS over a long session this would
        // reach the range where a float's precision makes the swing visibly jerky.
        if (m_swingPhase > kTwoPi) {
            m_swingPhase -= kTwoPi;
        }
    }

    const f32 target = swinging ? 1.0f : 0.0f;
    m_swingAmount += (target - m_swingAmount) * std::min(1.0f, dt * 14.0f);
    if (!swinging && m_swingAmount < 0.01f) {
        m_swingAmount = 0.0f;
        m_swingPhase = 0.0f; // Next swing starts from the top of the arc.
    }
}

void Engine::updateBreaking(f32 dt) {
    const bool holding = m_input->isDown(MouseButton::Left) && m_input->cursorCaptured();

    // The arm goes up whenever the button is down and something is in reach, even if
    // the block turns out to be unbreakable. Swinging at bedrock and having nothing
    // happen is the correct feedback; not swinging at all reads as broken input.
    updateSwing(dt, holding && m_target.has_value());

    // Abandon the swing if the button came up, the crosshair left the block, or the
    // block stopped being breakable under it. Any of those resets to zero rather
    // than pausing: partial progress that survives looking away would let a player
    // chip at four blocks at once by sweeping the crosshair between them.
    const bool sameBlock = m_target.has_value() && m_breakingBlock.has_value()
                        && m_target->block == *m_breakingBlock;

    if (!holding || !m_target.has_value() || !sameBlock) {
        m_breakingBlock = m_target.has_value() && holding
                              ? std::optional<BlockPos>{m_target->block}
                              : std::nullopt;
        m_breakProgress = 0.0f;
        if (!m_breakingBlock.has_value()) {
            return;
        }
    }

    const BlockId block = m_world->blockAt(*m_breakingBlock);
    if (block == kAirBlock || isUnbreakable(block)) {
        // Bedrock is the world's floor and the reason you cannot fall out of the
        // bottom of it. Vanilla refuses for the same reason, and the crack overlay
        // never appears on it because progress never advances.
        m_breakProgress = 0.0f;
        return;
    }

    const f32 seconds = breakSeconds(block);
    if (seconds <= 0.0f) {
        breakTargetBlock(); // Hardness zero: gone on contact.
        m_breakingBlock.reset();
        m_breakProgress = 0.0f;
        return;
    }

    m_breakProgress += dt / seconds;
    if (m_breakProgress < 1.0f) {
        return;
    }

    breakTargetBlock();
    m_breakingBlock.reset();
    m_breakProgress = 0.0f;
}

vec3 Engine::playerFeet() const {
    return m_camera.position() - Camera::up() * CharacterRenderer::kEyeHeight;
}

void Engine::placeTargetBlock() {
    if (!m_target.has_value()) {
        return;
    }

    const BlockPos target = m_target->adjacent;

    // Do not place a block inside the player. The collision here is deliberately the
    // same shape as the one walking uses -- a column of two blocks at the feet, no
    // width -- so that placing and standing agree about where the player is. A
    // narrower test would let you seal yourself into a block you are standing in.
    const vec3 feet = playerFeet();
    const auto feetX = static_cast<i32>(std::floor(feet.x));
    const auto feetZ = static_cast<i32>(std::floor(feet.z));
    const auto feetY = static_cast<i32>(std::floor(feet.y + 0.01f));

    if (target.x == feetX && target.z == feetZ && (target.y == feetY || target.y == feetY + 1)) {
        return;
    }

    // Whatever is in the selected slot, which is now a real slot rather than a fixed
    // block type. An empty slot places nothing, and the hotbar already shows it empty.
    const ItemStack& held = m_inventory.at(m_hotbarSlot);
    if (held.empty()) {
        return;
    }
    const BlockId block = held.block;

    // Taken only once the edit is known to have landed, so a placement refused for
    // being outside the world does not silently cost a block.
    if (applyEdit(target, block)) {
        m_inventory.takeOne(m_hotbarSlot);
        ++m_blocksPlaced;
    }
}

vec2 Engine::cursorNdc() const {
    const auto width = static_cast<f32>(std::max(1, m_window->framebufferWidth()));
    const auto height = static_cast<f32>(std::max(1, m_window->framebufferHeight()));

    // Window pixels are y-down and the HUD is y-up, so the vertical axis flips here
    // and nowhere else. Getting this wrong makes the top row of slots respond to
    // clicks on the bottom row, which looks like a layout bug rather than a sign
    // error.
    return vec2{static_cast<f32>(m_input->mouseX()) / width * 2.0f - 1.0f,
                1.0f - static_cast<f32>(m_input->mouseY()) / height * 2.0f};
}

void Engine::toggleInventory() {
    m_inventoryOpen = !m_inventoryOpen;

    if (m_inventoryOpen) {
        m_input->setCursorCaptured(false);
        return;
    }

    // Closing with a stack in hand must not delete it. It goes back into the
    // inventory, and whatever does not fit is dropped at the player's feet -- which
    // is vanilla's answer and is why `releaseCursor` hands the remainder back rather
    // than swallowing it.
    const ItemStack leftover = m_inventory.releaseCursor();
    if (!leftover.empty()) {
        m_items.spawn(m_camera.position(), leftover.block, leftover.count);
    }

    m_input->setCursorCaptured(true);
}

void Engine::updateInventoryScreen() {
    const vec2 cursor = cursorNdc();

    const auto aspect = static_cast<f32>(m_window->framebufferWidth())
                      / static_cast<f32>(std::max(1, m_window->framebufferHeight()));
    const InventoryLayout layout{aspect};

    const bool left = m_input->wasPressed(MouseButton::Left);
    const bool right = m_input->wasPressed(MouseButton::Right);
    if (!left && !right) {
        return;
    }

    const std::optional<usize> slot = layout.hitTest(cursor.x, cursor.y);
    if (!slot.has_value()) {
        // Clicking outside every slot. Vanilla throws the held stack into the world;
        // this drops it at the player's feet, which is the same idea without needing
        // a throw velocity nothing else would use.
        if (left && !m_inventory.cursorEmpty()) {
            const ItemStack thrown = m_inventory.releaseCursor();
            if (!thrown.empty()) {
                m_items.spawn(m_camera.position(), thrown.block, thrown.count);
            }
        }
        return;
    }

    if (left) {
        m_inventory.clickSlot(*slot);
    } else {
        m_inventory.splitSlot(*slot);
    }
}

void Engine::updateInteraction(f32 dt) {
    MC_PROFILE_SCOPE_N("Engine::updateInteraction");

    // Retry whatever was blocked last frame, before casting anything new. Draining
    // in place rather than clearing: an edit can be blocked again, and it keeps its
    // age so a genuinely stuck one is still noticed.
    if (!m_pendingEdits.empty()) {
        std::vector<PendingEdit> retry;
        retry.swap(m_pendingEdits);

        for (PendingEdit& edit : retry) {
            const World::EditStatus status = m_world->setBlock(edit.pos, edit.block);
            if (status != World::EditStatus::Busy) {
                // Landing late is still landing, and the neighbours have to hear
                // about it. This path does not go through `applyEdit`, so the
                // notification has to be repeated here rather than inherited.
                if (status == World::EditStatus::Applied
                    || status == World::EditStatus::Unchanged) {
                    m_blockUpdates.notify(edit.pos);
                }
                continue;
            }
            if (++edit.age >= kMaxEditAge) {
                // A pin held this long is a bug somewhere else -- a leaked pin or a
                // lost job -- and dropping the edit quietly would hide it.
                logWarn("Dropping a block edit at ({}, {}, {}): column pinned for {} frames",
                        edit.pos.x, edit.pos.y, edit.pos.z, edit.age);
                continue;
            }
            m_pendingEdits.push_back(edit);
        }
    }

    // Dropped blocks fall, settle and merge, then anything within arm's reach goes
    // into the inventory. Ticked before the aim ray so an item spawned last frame is
    // already where it looks like it is.
    m_items.tick(*m_world, dt);
    m_itemSpin += dt;

    // **Measured from the body, not from the eye.** Passing `m_camera.position()`
    // here is what stopped an item at the player's feet ever being collected -- the
    // eye is 1.62 up and the item rests 0.12 up, so standing on one put it 1.50 away
    // against a 1.4 radius. `PickupVolume` carries the account of it.
    //
    // Offered rather than taken: with stack limits the inventory can be full, and a
    // stack that does not fit stays on the ground instead of being deleted.
    m_items.collectInto(ItemEntities::PickupVolume{playerFeet(),
                                                  CharacterRenderer::kHeight,
                                                  ItemEntities::kPickupRadius},
                        [this](BlockId block, u32 count) {
                            const u32 leftover = m_inventory.add(block, count);
                            m_itemsCollected += count - leftover;
                            return leftover;
                        });

    if (m_input->wasPressed(Key::E)) {
        toggleInventory();
    }

    if (m_inventoryOpen) {
        // The world is not aimed at, broken, placed into or looked around while the
        // window is up. Clearing the target rather than leaving the last one is what
        // stops a stale selection box hanging in the world behind the panel.
        m_target.reset();
        m_breakingBlock.reset();
        m_breakProgress = 0.0f;
        updateSwing(dt, false);
        updateInventoryScreen();
        return;
    }

    // Aim from the player's eye, never from the render camera. In third person the
    // render camera sits several blocks behind, and casting from there would target
    // whatever is between the camera and the player.
    m_target = raycast(*m_world, m_camera.position(), m_camera.forward(), kReachDistance);

    if (!m_input->cursorCaptured()) {
        // Escape releases the cursor, so a click is how it comes back -- and that
        // click must not also dig a hole. Returning here is what separates the two.
        if (m_input->wasPressed(MouseButton::Left)) {
            m_input->setCursorCaptured(true);
        }
        m_breakingBlock.reset();
        m_breakProgress = 0.0f;
        updateSwing(dt, false);
        return;
    }

    for (usize slot = 0; slot < Inventory::kHotbarSlots; ++slot) {
        const auto key = static_cast<Key>(static_cast<u32>(Key::Num1) + slot);
        if (m_input->wasPressed(key)) {
            m_hotbarSlot = slot;
            const ItemStack& held = m_inventory.at(slot);
            logInfo("Holding: {}",
                    held.empty() ? "nothing" : kBlocks[held.block].name);
        }
    }

    // Breaking is held, not clicked. Placing stays edge-triggered: vanilla repeats
    // breaking on a timer and does not repeat placing at all, and a place-repeat at
    // 60 Hz would lay sixty blocks a second along the view ray.
    updateBreaking(dt);

    if (m_input->wasPressed(MouseButton::Right)) {
        placeTargetBlock();
    }
}

void Engine::updateTicks(f32 dt) {
    MC_PROFILE_SCOPE_N("Engine::updateTicks");

    // Falling blocks move on frame time, not on the tick. A cube descending in 20 Hz
    // steps against a 60 FPS camera visibly stutters, and interpolating a position
    // the simulation already knows exactly would be work to hide work.
    // `ItemEntities` made the same call for the same reason.
    const FallingBlocks::Result landings = m_falling.tick(*m_world, dt);

    for (const BlockPos& pos : landings.landed) {
        // A landing is an edit like any other: it can be what a block above was
        // waiting for.
        m_blockUpdates.notify(pos);
    }
    for (const FallingBlocks::Displaced& lost : landings.displaced) {
        // Landed where something else had appeared. Vanilla drops it rather than
        // deleting it, and the item system to do that with already exists.
        m_items.spawn(lost.position, lost.block, 1);
    }

    // The scheduler itself is on the fixed clock, because "one block per tick" is
    // what gives a collapse its cascade and it must not depend on the frame rate.
    m_tickAccumulator += dt;

    const f32 backlog = kTickSeconds * static_cast<f32>(kMaxTicksPerFrame);
    if (m_tickAccumulator > backlog) {
        m_tickAccumulator = backlog;
    }

    while (m_tickAccumulator >= kTickSeconds) {
        m_tickAccumulator -= kTickSeconds;
        m_blockUpdates.tick(*m_world, m_falling);
    }
}

std::optional<f32> Engine::groundBelow(f32 x, f32 z, f32 fromY) const {
    const auto blockX = static_cast<i32>(std::floor(x));
    const auto blockZ = static_cast<i32>(std::floor(z));

    const i32 start = std::min(kWorldMaxY - 1, static_cast<i32>(std::floor(fromY)));
    const i32 stop = std::max(kWorldMinY, start - kGroundSearchDepth);

    for (i32 y = start; y >= stop; --y) {
        // Solid, not merely non-air. Water holds nothing up, and before it existed
        // these two tests were the same thing -- a player stood on the sea otherwise.
        if (isSolidBlock(m_world->blockAt(BlockPos{blockX, y, blockZ}))) {
            return static_cast<f32>(y + 1); // Stand on top of it.
        }
    }
    return std::nullopt;
}

bool Engine::inWater(const vec3& feet) const {
    const BlockPos block{static_cast<i32>(std::floor(feet.x)),
                         static_cast<i32>(std::floor(feet.y + 0.1f)),
                         static_cast<i32>(std::floor(feet.z))};
    return isFluid(m_world->blockAt(block));
}

void Engine::updateWalk(f32 dt) {
    vec3 feet = playerFeet();

    // Input is flattened onto the ground plane. This is the whole difference
    // between walking and flying: looking up must not lift you off the floor.
    vec3 forward = m_camera.forward();
    vec3 right = m_camera.right();
    forward.y = 0.0f;
    right.y = 0.0f;
    if (math::dot(forward, forward) > 1e-6f) { forward = math::normalize(forward); }
    if (math::dot(right, right) > 1e-6f) { right = math::normalize(right); }

    // Movement keys are ignored while the inventory is up -- W would otherwise walk
    // the player off a cliff behind the panel. **Gravity is not**: the world does not
    // pause in singleplayer Minecraft either, and a player who opens their inventory
    // mid-fall should still land.
    vec3 wish{0.0f};
    if (!m_inventoryOpen) {
        if (m_input->isDown(Key::W)) { wish += forward; }
        if (m_input->isDown(Key::S)) { wish -= forward; }
        if (m_input->isDown(Key::D)) { wish += right; }
        if (m_input->isDown(Key::A)) { wish -= right; }
    }

    if (math::dot(wish, wish) > 0.0f) {
        const f32 speed = m_input->isDown(Key::LeftControl)  ? kSprintSpeed
                          : m_input->isDown(Key::LeftShift) ? kSneakSpeed
                                                            : kWalkSpeed;
        const vec3 target = feet + math::normalize(wish) * speed * dt;

        // Accepted or refused whole, by asking how high the ground is where the
        // step would land. Anything within a step is walked up; anything taller is
        // a wall. No swept volume, so a corner can be cut -- worth knowing before
        // trusting this for anything but looking around.
        const auto ground = groundBelow(target.x, target.z, feet.y + kStepHeight);
        if (ground.has_value() && *ground - feet.y <= kStepHeight) {
            feet.x = target.x;
            feet.z = target.z;
            if (m_onGround && *ground > feet.y) {
                feet.y = *ground; // Step up onto it rather than bumping into it.
            }
        }
    }

    // **Swimming, such as it is.** Water holds nothing up, so without this the player
    // sinks to the sea bed at full gravity and walks around down there. Buoyancy is a
    // quarter of gravity with a low sink speed, and Space paddles upward -- which is
    // enough to swim out to an island and back, and is not vanilla's model (no air
    // meter, no drowning, no swimming pose).
    const bool swimming = inWater(feet);

    if (!m_inventoryOpen && m_input->isDown(Key::Space)) {
        if (swimming) {
            m_verticalVelocity = kSwimSpeed;
        } else if (m_onGround) {
            m_verticalVelocity = kJumpVelocity;
            m_onGround = false;
        }
    }

    if (swimming) {
        m_verticalVelocity =
            std::max(m_verticalVelocity - kGravity * kSwimGravityScale * dt, -kSinkSpeed);
        // Entering water cancels a fall, which is vanilla's rule and the reason
        // jumping off a cliff into a lake is a thing people do.
        m_trackingFall = false;
    } else {
        m_verticalVelocity =
            std::max(m_verticalVelocity - kGravity * dt, -kTerminalVelocity);
    }

    feet.y += m_verticalVelocity * dt;

    const bool wasOnGround = m_onGround;

    const auto ground = groundBelow(feet.x, feet.z, feet.y + 0.01f);
    if (!ground.has_value()) {
        // Nothing under us -- almost always a column that has not streamed in yet.
        // Hold height rather than falling through the world while it arrives, which
        // is the same call followGround makes in the benchmark.
        feet.y -= m_verticalVelocity * dt;
        m_verticalVelocity = 0.0f;

        // And do not let that count as a fall. A column arriving late is an engine
        // detail, and taking damage for it would be the game punishing the player
        // for the streaming pipeline.
        m_trackingFall = false;
    } else if (feet.y <= *ground) {
        feet.y = *ground;
        m_verticalVelocity = 0.0f;
        m_onGround = true;

        if (m_trackingFall) {
            applyFallDamage(m_fallFromY, feet.y);
            m_trackingFall = false;
        }
    } else {
        m_onGround = false;

        // Start measuring from the height we left the ground at, not from the peak.
        // A jump therefore costs its own arc, which is why the three-block grace
        // exists at all -- vanilla measures the same way.
        if (wasOnGround) {
            m_fallFromY = feet.y;
            m_trackingFall = true;
        } else if (feet.y > m_fallFromY) {
            // Still rising. The fall has not started yet.
            m_fallFromY = feet.y;
        }
    }

    m_camera.setPosition(feet + Camera::up() * CharacterRenderer::kEyeHeight);
}

void Engine::applyFallDamage(f32 fromY, f32 toY) {
    const f32 distance = fromY - toY;
    if (distance <= kSafeFallBlocks) {
        return;
    }

    // Vanilla's formula: one half-heart per block past the third, rounded down. At
    // 20 health that means a 23-block drop is fatal, which is close enough to the
    // real thing that the height a player learns to fear transfers.
    const auto damage = static_cast<f32>(
        static_cast<i32>(std::floor(distance - kSafeFallBlocks)));
    if (damage <= 0.0f) {
        return;
    }

    m_health = std::max(0.0f, m_health - damage);
    logInfo("Fell {:.1f} blocks: -{:.0f} health, {:.0f} left",
            distance, damage, m_health);

    if (m_health <= 0.0f) {
        respawn();
    }
}

void Engine::respawn() {
    // No death screen and no dropped inventory: both are real vanilla behaviour and
    // both need a decision this has not earned yet -- a screen needs the UI layer to
    // grow a second window, and dropping the inventory needs somewhere for it to go
    // that the player can get back to. Full health where you stand is the honest
    // placeholder, and it says so in the log rather than pretending nothing happened.
    m_health = kMaxHealth;
    m_verticalVelocity = 0.0f;
    m_trackingFall = false;
    logWarn("You died. Respawning with full health where you stand.");
}

void Engine::updateFly(f32 dt) {
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

    // Cast the aim ray first, so a capture shows the same frame the player would see
    // -- selection box included. Without this the one tool that can look at a frame
    // without a compositor is blind to the one thing drawn only when aiming at
    // something, which is exactly the thing worth checking.
    //
    // Zero dt: a capture is one frame out of time, and no break should advance in it.
    updateInteraction(0.0f);

    renderFrame();
    const std::vector<u8> pixels = m_device->readFramebufferRgba(width, height);
    writePpm(m_options.capturePath, width, height, pixels);

    const ChunkRenderer::Stats& stats = m_chunkRenderer->stats();
    logInfo("Captured {}x{} frame to {} ({} sections, {} quads drawn)",
            width, height, m_options.capturePath, stats.sectionsDrawn, stats.quadsDrawn);

    // What the crosshair is on, in words. The selection box is a few dark pixels in
    // a 1280x720 image and is genuinely hard to confirm by eye; this says outright
    // whether the aim ray found anything and what.
    if (m_target.has_value()) {
        logInfo("Aiming at {} at ({}, {}, {}), face {}, {:.2f} blocks away",
                kBlocks[m_world->blockAt(m_target->block)].name,
                m_target->block.x, m_target->block.y, m_target->block.z,
                static_cast<u32>(m_target->face), m_target->distance);
    } else {
        logInfo("Aiming at nothing within {:.1f} blocks", kReachDistance);
    }
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

    // The interaction half of the line. Separate from the rendering half because it
    // answers a different question -- "did the player do anything" rather than "is
    // the engine keeping up" -- and because three sessions of logs that could answer
    // only the second one is what put it here.
    logInfo("  broke {} | placed {} | collected {} | {} items, {} falling, "
            "{} updates queued",
            m_blocksBroken, m_blocksPlaced, m_itemsCollected,
            m_items.size(), m_falling.size(), m_blockUpdates.pending());
}

void Engine::stepFrame(f64 deltaTime) {
    (void)deltaTime;

    // Return ranges retired long enough ago before allocating any new ones.
    m_meshStore->recycle(m_frame);

    // Before submitting, so a block broken this frame is remeshed this frame rather
    // than sitting visibly intact until the next one.
    updateInteraction(static_cast<f32>(deltaTime));

    // After interaction and before streaming, for the same reason: a block that
    // falls because of this frame's click should be gone from the mesh this frame.
    updateTicks(static_cast<f32>(deltaTime));

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

void Engine::followGround() {
    const vec3& position = m_camera.position();
    const i32 blockX = static_cast<i32>(std::floor(position.x));
    const i32 blockZ = static_cast<i32>(std::floor(position.z));

    // Read the world that is already loaded rather than asking the generator. A
    // surfaceHeight() call evaluates a whole column of density, and doing that once a
    // frame would put terrain generation inside the thing being measured.
    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        if (m_world->blockAt(BlockPos{blockX, y, blockZ}) != kAirBlock) {
            m_camera.setPosition({position.x,
                                  static_cast<f32>(y) + kBenchEyeHeight,
                                  position.z});
            return;
        }
    }
    // Nothing solid below -- the column has not streamed in yet. Hold the current
    // height rather than dropping into the void.
}

void Engine::runBenchmark() {
    m_window->setVsync(false);

    /// Roughly a sprinting player. Fast enough that columns cross the region boundary
    /// continuously, so streaming is part of what gets measured.
    constexpr f32 kFlySpeed = 40.0f;
    /// A frame longer than this is a stall, not a step; do not let the camera lurch.
    constexpr f64 kMaxStep = 0.1;

    std::vector<f64> frameMs;
    frameMs.reserve(4096);

    const vec3 start = m_camera.position();

    Clock clock;
    f64 previous = clock.elapsed();

    while (clock.elapsed() < m_options.benchSeconds && !m_window->shouldClose()) {
        // Standard delta time: `step` is how long the *previous* frame took, and it is
        // both what the camera advances by and what gets recorded. Reading the clock
        // at the top while updating `previous` at the bottom measures the gap between
        // iterations instead of the frame -- which is nearly zero, and left the camera
        // moving 11 blocks in 20 seconds instead of 800.
        const f64 now = clock.elapsed();
        const f64 step = std::min(now - previous, kMaxStep);
        previous = now;

        m_window->pollEvents();

        // Horizontally, then follow the ground.
        //
        // Flying along the view direction was wrong and quietly invalidated every
        // number this produced: the spawn pitch is -0.18, so 2,000 blocks of travel
        // sank the camera 358 blocks and out through the bottom of the world, and the
        // later frames measured an underground view with almost nothing in it.
        vec3 forward = m_camera.forward();
        forward.y = 0.0f;
        if (math::dot(forward, forward) > 0.0f) {
            m_camera.move(math::normalize(forward) * kFlySpeed * static_cast<f32>(step));
        }
        followGround();

        stepFrame(step);
        m_window->swapBuffers();

        frameMs.push_back(step * 1000.0);
    }

    const f32 travelled = math::length(m_camera.position() - start);

    if (frameMs.size() < 2) {
        logWarn("Benchmark produced no frames");
        return;
    }
    // The first sample is the gap before the loop started, not a frame. Dropping it
    // is why `min` is a real number rather than 0.00.
    frameMs.erase(frameMs.begin());

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
    logInfo("--- {:.0f} s, {} frames, vsync off, render distance {} ---",
            m_options.benchSeconds, frameMs.size(), m_options.renderDistance);
    logInfo("camera flew {:.0f} blocks ({:.0f} columns) at {} blocks/s",
            travelled, travelled / static_cast<f32>(kSectionSize), 40);
    logInfo("mean {:.2f} ms ({:.0f} FPS) | median {:.2f} | p99 {:.2f} | min {:.2f} | max {:.2f}",
            mean, 1000.0 / mean, percentile(0.5), percentile(0.99), sorted.front(), sorted.back());

    const ChunkRenderer::Stats& stats = m_chunkRenderer->stats();
    logInfo("last frame: {} sections drawn, {} quads, {} columns + {} sections culled",
            stats.sectionsDrawn, stats.quadsDrawn, stats.columnsCulled, stats.sectionsCulled);
    const vec3& end = m_camera.position();
    const World::HeldCounts held = m_world->heldCounts(cameraColumn());
    const auto expected = static_cast<usize>(2 * m_options.renderDistance + 1);
    logInfo("camera ended at {:.0f},{:.0f},{:.0f} (column {},{})",
            end.x, end.y, end.z, cameraColumn().x, cameraColumn().z);
    logInfo("columns loaded {} (region wants {}), outside region {}, pinned {}, generating {}",
            m_world->loadedChunkCount(), expected * expected,
            held.outsideRegion, held.pinned, held.generating);

    // A backlog here means the camera outran streaming, and the frame times above
    // describe a world with holes in it rather than the one a player would see.
    if (held.generating > expected) {
        logWarn("streaming did not keep up: {} columns still generating -- the frame "
                "times above are measuring an incomplete world",
                held.generating);
    }
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

    if (m_options.benchSeconds > 0.0) {
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

    updateRenderCamera();

    const vec3& sky = skyColorLinear();
    m_device->clear(sky.x, sky.y, sky.z, 1.0f);

    buildVisibleSet();
    m_chunkRenderer->draw(*m_device, m_renderCamera, *m_meshStore);

    // After the terrain, so the character is depth-tested against a filled buffer
    // rather than against nothing. Only in third person: in first person the model
    // is around the eye and would fill the screen with the inside of a head.
    if (m_thirdPerson) {
        const vec3 feet = playerFeet();
        m_character->draw(*m_device, m_renderCamera, feet, m_camera.forward(),
                          m_walkPhase, m_walkAmount, m_swingPhase, m_swingAmount);
    }

    m_itemRenderer->draw(*m_device, m_renderCamera, m_chunkRenderer->textures(), m_items,
                         m_falling, m_itemSpin);

    // Last, so the outline sits on top of the block it surrounds and of the
    // character if one is in the way. It still depth-tests -- it is inflated just
    // enough to win against its own block's faces and nothing else -- so a wall
    // between the player and the target correctly hides it.
    if (m_target.has_value()) {
        const vec3 origin{static_cast<f32>(m_target->block.x),
                          static_cast<f32>(m_target->block.y),
                          static_cast<f32>(m_target->block.z)};
        m_selection->draw(*m_device, m_renderCamera, origin);

        // Only while a break is actually running on *this* block. Progress is reset
        // the moment the crosshair leaves it, so the cracks cannot be left behind on
        // a block the player walked away from.
        if (m_breakProgress > 0.0f && m_breakingBlock.has_value()
            && *m_breakingBlock == m_target->block) {
            m_selection->drawCracks(*m_device, m_renderCamera, origin,
                                    SelectionRenderer::stageFor(m_breakProgress));
        }
    }

    // Last of all, because it clears depth. In third person the arm is already on
    // the character and a second one floating in front of the camera would be one
    // arm too many.
    if (!m_thirdPerson) {
        m_character->drawHand(*m_device, m_renderCamera, m_swingPhase, m_swingAmount);
    }

    // The HUD is genuinely last: it draws over everything, including the hand.
    const auto width = static_cast<f32>(m_window->framebufferWidth());
    const auto height = static_cast<f32>(m_window->framebufferHeight());
    const vec2 cursor = cursorNdc();

    HudRenderer::State hud;
    hud.hotbarSlot = m_hotbarSlot;
    hud.health = m_health;
    hud.maxHealth = kMaxHealth;
    hud.inventoryOpen = m_inventoryOpen;
    hud.cursorX = cursor.x;
    hud.cursorY = cursor.y;

    m_hud->draw(*m_device, m_chunkRenderer->textures(), m_inventory, hud,
                width / std::max(1.0f, height));
}

void Engine::updateRenderCamera() {
    m_renderCamera = m_camera;
    if (!m_thirdPerson) {
        return;
    }

    // Back along the view direction, pulled in when terrain is in the way.
    //
    // This used to be an unconditional step backwards, with a note that fixing it
    // needed a voxel raycast the engine did not have. Phase 9 built one for aiming,
    // and this is the second caller: cast backwards from the eye and stop short of
    // whatever is hit. Minecraft does exactly this.
    constexpr f32 kThirdPersonDistance = 4.0f;
    /// Kept between the camera and the wall, so the near plane does not clip into it
    /// and show the inside of the block.
    constexpr f32 kWallMargin = 0.25f;

    const vec3 back = -m_camera.forward();
    f32 distance = kThirdPersonDistance;

    if (const auto hit = raycast(*m_world, m_camera.position(), back, kThirdPersonDistance)) {
        distance = std::max(0.0f, hit->distance - kWallMargin);
    }

    m_renderCamera.setPosition(m_camera.position() + back * distance);
}

} // namespace mc
