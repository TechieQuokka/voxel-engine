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
#include <stop_token>
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
    m_player.flying = m_options.flying;

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

    m_world = std::make_unique<World>(m_options.renderDistance);
    m_generator = std::make_unique<Generator>();

    // Opened before any worker exists, because the generation job reads from it.
    //
    // **Failure here throws rather than dropping to a session that cannot save.**
    // This is an init boundary, where exceptions are the convention (DESIGN.md 6.2),
    // and the alternative is worse than not starting: a player who opened the wrong
    // directory, or whose disk is full, would play for an hour and lose it at the
    // door with only a line in the log to say so.
    if (!m_options.noSave) {
        const std::filesystem::path savePath =
            m_options.savePath.empty() ? executableDir() / "saves" / "world"
                                       : std::filesystem::path(m_options.savePath);

        Result<std::unique_ptr<WorldStore>, WorldStore::Error> store =
            WorldStore::open(savePath, m_generator->seed());
        if (!store) {
            throw std::runtime_error(std::string("Cannot open the world save: ")
                                     + describe(store.error()));
        }
        m_store = std::move(store).value();
        logInfo("World save: {}", m_store->directory().string());
    }

    // **The player is read here, above everything that seeds one.**
    //
    // The order is the whole of the rule: the save replaces the spawn, and the
    // command line replaces the save. `--hold`, `--inventory`, `--furnace` and
    // `--damaged` all write into the player below, and every one of them is somebody
    // saying what this run should look like -- a `--hold wooden_pickaxe` that a saved
    // inventory silently overrode would make the capture flags useless against any
    // world that had been played once, which is every world worth capturing.
    //
    // A failed load is not fatal. The columns are still readable and the run is still
    // worth having; what is lost is one session's inventory, and `loadPlayer` has
    // already logged and counted it.
    if (m_store) {
        auto loaded = m_store->loadPlayer();
        if (loaded && loaded.value().has_value()) {
            m_player = std::move(loaded).value().value();
            m_playerCameFromSave = true;
            logInfo("Loaded the player: {:.1f} health, {} slots used, at "
                    "({:.1f}, {:.1f}, {:.1f}){}",
                    m_player.health, m_player.inventory.usedSlots(), m_player.position.x,
                    m_player.position.y, m_player.position.z,
                    m_player.flying ? ", flying" : "");
        } else if (!loaded) {
            // Said once, at the point it happened. The stats line's `FAILED` counter
            // carries it for the rest of the session.
            logWarn("Spawning fresh: the saved player could not be read");
        }
    }

    // **Every line of this is fixture data for a capture flag, and it lives in
    // app/CaptureScenarios.cpp.** None of it runs in an ordinary session. What stays
    // here is only its position: after `m_input`, because opening a screen releases
    // the cursor; after the saved player, because these deliberately override it; and
    // before the renderers, so a bad `--hold` name fails before a window exists.
    seedCaptureScenario();

    m_chunkRenderer.emplace();
    m_character.emplace();
    m_selection.emplace();
    m_itemRenderer.emplace();
    m_hud.emplace();
    m_meshStore.emplace(meshArenaBytesFor(m_options.renderDistance));
    m_frameRing.emplace(frameRingBytesFor(m_options.renderDistance));

    // Worth one line: FastNoise2 dispatches on the CPU at runtime, and the gap between
    // AVX2 and SSE2 is large enough that a generation timing is meaningless without
    // knowing which one ran.
    logInfo("Terrain noise: FastNoise2 on {}, density grid {}x{}x{} per column "
            "({} samples for {} voxels)",
            m_generator->graph().simdLevelName(),
            DensityField::kGridX, DensityField::kGridY, DensityField::kGridZ,
            DensityField::kSampleCount,
            static_cast<usize>(kSectionSize) * kSectionSize * static_cast<usize>(kWorldHeight));

    // **Last of the subsystems, because it borrows four of the others.** The streamer
    // holds references to the World it fills, the Generator it fills it from, the
    // store it loads edited columns out of and the arena it uploads into, so it is
    // constructed after all four and -- being declared after them -- destroyed before
    // any of them. It starts its workers and its upload thread here.
    m_streamer = std::make_unique<ChunkStreamer>(*m_world, *m_generator, m_store.get(),
                                                 *m_meshStore, m_options.renderDistance);

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

    // **Only when the save did not already say where to stand.** A player who quit
    // in a cave and came back on the surface would have lost something that is not
    // in any file -- where they were -- and it is the one part of a save that is
    // noticed immediately.
    if (!m_playerCameFromSave) {
        // Feet on the block above the surface -- so the first frame is what standing
        // there looks like, not what hovering above it does. The eye follows from
        // `Player::eye()`; nothing here places it.
        m_player.position = {0.5f, static_cast<f32>(groundY + 1), 0.5f};
        m_player.yaw = 0.6f;
        m_player.pitch = -0.18f;
        m_player.onGround = true;
    }

    // `--at` overrides both, and is given as the *eye* rather than the feet: it
    // exists so a capture can be aimed at something, and what a capture frames is
    // where the eye is. Converted to feet on the way in, because the player is what
    // holds a position now. The ground probe will settle the feet on the next
    // walking step if the position is over solid terrain.
    if (m_options.cameraPosition.has_value()) {
        const vec3& eye = *m_options.cameraPosition;
        m_player.position = {eye.x, eye.y - PlayerBox::kEyeHeight, eye.z};
        m_player.onGround = false;
    }
    if (m_options.cameraOrientation.has_value()) {
        m_player.yaw = m_options.cameraOrientation->x;
        m_player.pitch = math::clamp(m_options.cameraOrientation->y, -Player::kMaxPitch,
                                     Player::kMaxPitch);
    }

    syncCamera();
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
        m_streamer->drain();
        const f64 seconds = clock.elapsed();

        const ChunkStreamer::Stats streaming = m_streamer->stats();
        logInfo("Warm-up: {} columns, {} sections meshed in {:.2f} s on {} workers",
                m_world->loadedChunkCount(), streaming.sectionsMeshed, seconds,
                m_streamer->workerCount());
        logInfo("  {} sections hold geometry, {} are fully enclosed and hold none",
                m_meshStore->sectionCount(), streaming.sectionsEmpty);
        m_reportedWarm = true;
    }

    applyStartupEdit();

    logInfo("Engine initialized (render distance {}, {} columns loaded)",
            m_options.renderDistance, m_world->loadedChunkCount());
}

void Engine::applyStartupEdit() {
    if (!m_options.editPosition.has_value()) {
        return;
    }

    const BlockPos pos = *m_options.editPosition;
    const std::optional<BlockId> block =
        BlockRegistry::instance().findByName(m_options.editBlock);
    if (!block.has_value()) {
        logError("--edit names '{}', which is not a block", m_options.editBlock);
        return;
    }

    // Read before writing, and log both. **The "before" is the whole point of this
    // flag**: run it twice against the same save and the second run reports the
    // block as already set, which nothing but a column that came back off disk can
    // produce.
    const BlockId before = m_world->blockAt(pos);

    // **Through `applyEdit`, not `World::setBlock`.** The first version called
    // `setBlock` directly and was therefore not a dig at all: `applyEdit` is what
    // calls `m_blockUpdates.notify`, and without it nothing in the world is told
    // anything happened -- no sand falls, and no water flows in. That made this flag
    // silently useless for the one thing it was about to be used for, and it read as
    // a bug in the water rather than in the harness.
    const bool ok = applyEdit(pos, *block);

    logInfo("--edit ({}, {}, {}): {} -> {} [{}]", pos.x, pos.y, pos.z,
            BlockRegistry::instance()[before].name, m_options.editBlock,
            before == *block ? "UNCHANGED (already set)" : ok ? "applied" : "refused");
}

Engine::~Engine() {
    // Streaming stops first. A worker part-way through loading a column is reading
    // the same region file this is about to write, and the store's lock would let
    // both happen in an order nobody chose.
    //
    // Explicitly rather than by letting `m_streamer` fall out of scope: the save below
    // must happen with every worker already stopped, and member destruction runs after
    // this body.
    if (m_streamer) {
        m_streamer->shutdown();
    }

    // After the workers and before the World: `m_world` is a member and outlives
    // this body, so every loaded column is still here to be written.
    saveEverything();
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

void Engine::syncCamera() {
    // The one place the camera learns where it is. Everything that moves or turns the
    // player writes `m_player` and calls this; nothing writes a position or an
    // orientation into the camera itself. That direction is the whole point of the
    // type -- with two writable copies of a position, the question "which one is
    // right" has to be answered at every call site, and item pickup is the bug that
    // gets when it is answered wrong.
    m_camera.setPosition(m_player.eye());
    m_camera.setOrientation(m_player.yaw, m_player.pitch);
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

    // What the streamer measures job priority from. Only changes here, which is the
    // only place the answer can change.
    m_streamer->setCentre(center);

    for (const std::unique_ptr<Chunk>& dropped : result.unloaded) {
        const ChunkPos pos = dropped->position();

        // **Before anything else touches it.** This is the last moment the column
        // exists; it is destroyed when `result` goes out of scope at the end of this
        // function.
        saveColumn(*dropped);

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

    if (result.created > 0 || !result.unloaded.empty()) {
        logDebug("Region {},{}: +{} -{} (retained {}), {} loaded",
                 center.x, center.z, result.created, result.unloaded.size(),
                 result.retained, m_world->loadedChunkCount());
    }
}

std::vector<SavedFurnace> Engine::furnacesIn(ChunkPos column) const {
    std::vector<SavedFurnace> found;
    for (const auto& [pos, furnace] : m_furnaces) {
        if (toChunkPos(pos) == column) {
            found.push_back(captureFurnace(pos, furnace));
        }
    }
    return found;
}

void Engine::saveColumn(const Chunk& chunk) {
    if (!m_store || !chunk.edited()) {
        return;
    }

    const ChunkPos pos = chunk.position();
    const std::vector<SavedFurnace> furnaces = furnacesIn(pos);

    // The failure is logged inside the store, which also counts it. Nothing here
    // can usefully recover -- the column is about to be destroyed either way -- so
    // the counter in the stats line is what makes a save that stopped working
    // visible, rather than it looking like a save with nothing to do.
    (void)m_store->saveColumn(chunk, furnaces);

    // Furnaces go with the column. Keeping them would leave the map growing with
    // every furnace the player ever walked past, and a stale one would be found by
    // a later right-click on a block that is no longer a furnace.
    if (!furnaces.empty()) {
        for (const SavedFurnace& saved : furnaces) {
            m_furnaces.erase(saved.position);
        }
    }
}

void Engine::saveEverything() {
    if (!m_store) {
        return;
    }

    // **Close the window first, and this is not tidiness.** A stack on the cursor and
    // the contents of a crafting grid are real items that live outside the thirty-six
    // slots, and `PlayerCodec` writes only the slots. Quitting with the inventory open
    // -- which the window's close button allows at any moment, `Escape` being only one
    // way out -- would otherwise write a player who is not holding what they were
    // holding. `closeScreen` is the routine that already knows how to give both back.
    //
    // What still will not fit gets dropped at the player's feet as an item entity and
    // is lost, because entities are not saved. That is the same answer the world
    // already gives for anything dropped on the ground, so it is consistent rather
    // than a new hole -- but it is the reason this runs before the encode and not
    // after.
    closeScreen();

    usize written = 0;
    m_world->forEachChunk([&](Chunk& chunk) {
        if (chunk.edited()) {
            (void)m_store->saveColumn(chunk, furnacesIn(chunk.position()));
            ++written;
        }
    });
    m_store->flush();

    // After the columns, and after the `closeScreen` at the top of this function has
    // put the cursor and any crafting cells back into the slots that are about to be
    // written. The failure is logged and counted inside the store.
    (void)m_store->savePlayer(m_player);

    const WorldStore::Stats stats = m_store->stats();
    logInfo("Saved {} columns and the player on exit "
            "({} written and {} loaded this session, {} failures)",
            written, stats.columnsSaved, stats.columnsLoaded, stats.failures);
}

void Engine::adoptLoadedFurnaces() {
    const std::vector<SavedFurnace> loaded = m_streamer->takeLoadedFurnaces();

    for (const SavedFurnace& saved : loaded) {
        applyFurnace(saved, m_furnaces[saved.position]);
    }
}

void Engine::relightArrivedColumns() {
    MC_PROFILE_SCOPE_N("Engine::relightArrivedColumns");

    const std::vector<ChunkPos> arrived = m_streamer->takeRelightQueue();
    if (arrived.empty()) {
        return;
    }

    // **Skipped while anything in reach is pinned, and re-queued rather than
    // dropped.** The flood writes light into the eight columns around this one, and a
    // mesher holding any of them is reading the very `LightArray` it would
    // reallocate. This is the same rule `World::setBlock` follows; the difference is
    // that there is no player waiting on the answer, so it simply comes back next
    // frame instead of reporting Busy.
    std::vector<ChunkPos> deferred;
    LightTouch touched;

    for (const ChunkPos& column : arrived) {
        // **The gate that keeps this off the streaming path.** Nine flag reads, and
        // in a world nobody has put a torch in they are all false and the column
        // costs nothing further. Without it every column that streams in walks a
        // hundred and eight section palettes to learn the same thing -- which is
        // measurable in the warm-up, since a render distance of 16 streams in a
        // thousand of them.
        bool anyEmitter = false;
        bool blocked = false;
        for (i32 dz = -1; dz <= 1; ++dz) {
            for (i32 dx = -1; dx <= 1; ++dx) {
                const Chunk* neighbour = m_world->find(ChunkPos{column.x + dx, column.z + dz});
                if (neighbour == nullptr) {
                    continue;
                }
                anyEmitter = anyEmitter || neighbour->hasEmitter();
                blocked = blocked || neighbour->pinned();
            }
        }
        if (!anyEmitter) {
            continue;
        }
        if (blocked) {
            deferred.push_back(column);
            continue;
        }

        touched.clear();
        seedBlockLight(*m_world, column, touched);
        m_world->dirtyAround(touched);
    }

    m_streamer->requeueRelight(deferred);
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
        if (screenOpen()) {
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
        m_player.rotate(static_cast<f32>(m_input->mouseDeltaX()) * kMouseSensitivity,
                        static_cast<f32>(-m_input->mouseDeltaY()) * kMouseSensitivity);
    }

    if (m_input->wasPressed(Key::F)) {
        m_player.flying = !m_player.flying;
        m_player.verticalVelocity = 0.0f;
        logInfo("Movement: {}", m_player.flying ? "flying" : "walking");
    }

    // **Before the move, not after.** Walking and flying both read `forward()` and
    // `right()` off the camera to turn input into a direction, so the turn above has
    // to have reached it by now or the player moves along last frame's heading.
    syncCamera();

    const vec3 startPosition = m_player.position;

    if (m_player.flying) {
        updateFly(dt);
    } else {
        updateWalk(dt);
    }

    syncCamera();

    // Drive the walk cycle from how far the player actually went, so the limbs are
    // in step with the ground rather than with the clock -- which also means they
    // stop dead when the player does.
    vec3 travelled = m_player.position - startPosition;
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
    // drops nothing. The held item is read here too, for the same reason -- the
    // harvest test has to run against what was in the hand when the block broke.
    const BlockId broken = m_world->blockAt(m_target->block);
    const ItemId tool = heldItem();

    if (!applyEdit(m_target->block, kAirBlock)) {
        return; // Not loaded, or outside the world. Nothing was removed, so nothing drops.
    }
    ++m_blocksBroken;

    // **Whatever the block was holding comes out with it.** A furnace is the only
    // block with contents so far; anything else this map ever holds gets the same
    // treatment here, because breaking is the one path that destroys a block.
    if (broken == kFurnaceBlock) {
        spillFurnace(m_target->block);
    }

    // **The block breaks whether or not it can be harvested, and only the drop is
    // withheld.** That is vanilla's rule and it is the one that teaches: a player who
    // punches stone for seven seconds and watches it vanish leaving nothing has been
    // told they need a pickaxe far more clearly than a refusal to break it would.
    const ItemId drop = dropOf(broken, tool);
    if (drop == kNoItem) {
        return; // Leaves, and anything the held tool was not good enough to harvest.
    }

    // At the centre of the block that was removed, which is where the space now is.
    //
    // **The count comes from the block, not from a 1.** A double slab is two slabs and
    // vanilla gives both back; without that, stacking two slabs and changing your mind
    // costs one of them, which is exactly the kind of quiet tax that stops a player
    // experimenting with a shape.
    m_items.spawn(vec3{static_cast<f32>(m_target->block.x) + 0.5f,
                       static_cast<f32>(m_target->block.y) + 0.5f,
                       static_cast<f32>(m_target->block.z) + 0.5f},
                  drop, dropCountOf(broken));
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

    // **What is held changes how long this takes, since Phase 16.** A matching tool
    // divides the time by its tier speed; a missing one multiplies it by 10/3,
    // because vanilla's no-harvest branch divides by 100 rather than 30. Bare-handed
    // stone is 7.5 seconds and yields nothing; a wooden pickaxe is 1.125 and yields
    // cobblestone.
    const f32 seconds = breakSeconds(block, heldItem());
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
    // **This used to be the subtraction that made the conversion possible to get
    // wrong**, and the accessor existed because four call sites spelled it out and a
    // fifth passed the eye instead -- the whole of the item-pickup bug. The player
    // holds the feet now, so there is nothing left to convert and nothing left to
    // convert backwards: `Player::eye()` is the only direction that exists.
    return m_player.position;
}

ItemId Engine::heldItem() const {
    // The rule it enforces -- an empty slot is `kNoItem`, not whatever the slot
    // happens to hold -- moved onto `Player` with the inventory and the selection it
    // reads, because that is where a test can reach it. This stays as the name the
    // four call sites here already use.
    return m_player.heldItem();
}

bool Engine::placeTargetBlock() {
    if (!m_target.has_value()) {
        return false;
    }

    // **A slab put into the free half of a cell that already holds one fills it**,
    // rather than going into the cell beyond. Decided before the player-box test
    // below, because the cell being filled is the one that was clicked and not the
    // empty one next to it -- and it is already occupied, so it cannot be inside the
    // player either.
    const vec3 aimPoint = m_camera.position() + m_camera.forward() * m_target->distance;
    const BlockId inCell = m_world->blockAt(m_target->block);
    const ItemStack& holding = m_player.inventory.at(m_player.hotbarSlot);
    if (!holding.empty()) {
        const BlockId heldForFill = blockOfItem(holding.item);
        if (heldForFill != kAirBlock
            && combinesIntoDoubleSlab(heldForFill, inCell, m_target->face)) {
            if (!applyEdit(m_target->block, doubleSlabFor(heldForFill))) {
                return false;
            }
            m_player.inventory.takeOne(m_player.hotbarSlot);
            ++m_blocksPlaced;
            return true;
        }
    }

    const BlockPos target = m_target->adjacent;

    // Do not place a block inside the player. **This used to be a two-block column at
    // the feet with no width**, copied from the shape walking uses, and the missing
    // width is what let a player seal a block into their own shoulder while building
    // a wall beside themselves. `PlayerBox` is the real 0.6-wide box, and it lives in
    // `world` so a test can reach it -- see the header for why that mattered twice.
    if (PlayerBox{playerFeet()}.intersects(target)) {
        return false;
    }

    // Whatever is in the selected slot, which is now a real slot rather than a fixed
    // block type. An empty slot places nothing, and the hotbar already shows it empty.
    const ItemStack& held = m_player.inventory.at(m_player.hotbarSlot);
    if (held.empty()) {
        return false;
    }

    // **Not every item is a block, since Phase 16.** Right-clicking with a pickaxe
    // does nothing rather than placing a mysterious cube, which is what would happen
    // if the id were used as a `BlockId` unchecked -- and it would be silent, because
    // the two share a type.
    const BlockId heldBlock = blockOfItem(held.item);
    if (heldBlock == kAirBlock) {
        return false;
    }

    // **Which half a slab lands in, decided by where on the block the ray met it.**
    // `distance` is carried on the hit precisely so the point is recoverable without
    // a second raycast; only the y of it matters, and only for a side face. Anything
    // that is not a slab comes back unchanged.
    const BlockId block = placedVariant(heldBlock, m_target->face, aimPoint.y);

    // Taken only once the edit is known to have landed, so a placement refused for
    // being outside the world does not silently cost a block.
    if (!applyEdit(target, block)) {
        return false;
    }

    m_player.inventory.takeOne(m_player.hotbarSlot);
    ++m_blocksPlaced;
    return true;
}

void Engine::updatePlacing(f32 dt) {
    // **Held, not clicked, and this reverses a decision that was right when it was
    // made.** `Input::wasPressed` used to carry the argument: repeating a place at
    // 60 Hz lays sixty blocks a second along the view ray, which is not building. The
    // conclusion that followed -- that no timer was worth having -- is the part that
    // did not survive contact with someone trying to build a house, because one click
    // per block means a wall costs a hundred distinct clicks. Vanilla repeats on a
    // 4-tick timer, and a timer is what makes holding the button *be* building.
    if (m_placeCooldown > 0.0f) {
        m_placeCooldown -= dt;
    }

    if (!m_input->isDown(MouseButton::Right)) {
        // Releasing arms the next press. Without this a deliberate single click made
        // within 0.2 s of the last one is silently dropped, which reads as the game
        // ignoring the mouse rather than as a repeat rate.
        m_placeCooldown = 0.0f;
        return;
    }

    if (m_placeCooldown > 0.0f) {
        return;
    }

    // **Using a block beats placing against it, and it is edge-triggered even though
    // placing is not.** Opening a window is not something to repeat five times a
    // second; holding the button after a table opens must not reopen it every tick.
    if (m_input->wasPressed(MouseButton::Right) && useTargetBlock()) {
        m_placeCooldown = kPlaceIntervalSeconds;
        return;
    }

    // The cooldown is spent only on a placement that actually happened. A player
    // sweeping the crosshair across the sky while holding the button is not using
    // anything up, so the first block they reach goes down immediately rather than
    // up to a fifth of a second later.
    if (placeTargetBlock()) {
        m_placeCooldown = kPlaceIntervalSeconds;
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

void Engine::openScreen(ScreenKind kind) {
    if (screenOpen()) {
        closeScreen();
    }

    m_screenKind = kind;

    if (kind == ScreenKind::CraftingTable) {
        // A table's grid exists only while its window is open. Vanilla is the same:
        // walk away mid-recipe and the cells fall out at your feet.
        m_tableCraft.emplace(3);
        m_screen.emplace(m_player.inventory, *m_tableCraft);
    } else if (kind == ScreenKind::Furnace) {
        // The opposite: a furnace outlives its window, so the screen points at the
        // one that lives in the map rather than at a fresh one.
        m_screen.emplace(m_player.inventory, m_furnaces[m_openFurnace]);
    } else {
        m_screen.emplace(m_player.inventory, m_playerCraft);
    }

    m_input->setCursorCaptured(false);
}

void Engine::closeScreen() {
    if (!screenOpen()) {
        return;
    }

    // **Nothing the player put in may evaporate because a window shut.** The stack in
    // hand and every filled crafting cell come back, and whatever the pack has no room
    // for is dropped at the player's feet -- which is vanilla's answer and is why
    // `releaseOne` hands a remainder back rather than swallowing it.
    //
    // One loop for both, because `Screen::releaseOne` is the one thing that knows what
    // this screen owes: a crafting grid gives its cells back and a chest gives nothing.
    //
    // **The loop ends on `moved`, not on an empty stack.** Ending on the stack was the
    // first version and it stopped after the first crafting cell, because a cell that
    // fitted into storage spills nothing and looks exactly like having nothing left.
    for (Screen::Release step = m_screen->releaseOne(); step.moved;
         step = m_screen->releaseOne()) {
        if (!step.spilled.empty()) {
            m_items.spawn(m_camera.position(), step.spilled.item, step.spilled.count);
        }
    }

    m_screen.reset();
    m_tableCraft.reset();

    // A furnace nobody put anything into is forgotten again, so opening one to look
    // inside costs nothing permanent. One that is burning or holding something stays,
    // and keeps burning with the window shut.
    if (m_screenKind == ScreenKind::Furnace) {
        const auto entry = m_furnaces.find(m_openFurnace);
        if (entry != m_furnaces.end() && entry->second.idle()) {
            m_furnaces.erase(entry);
        }
    }

    m_screenKind = ScreenKind::Player;

    m_input->setCursorCaptured(true);
}

void Engine::toggleInventory() {
    if (screenOpen()) {
        closeScreen();
    } else {
        openScreen(ScreenKind::Player);
    }
}

bool Engine::useTargetBlock() {
    if (!m_target.has_value()) {
        return false;
    }

    // Sneaking suppresses the interaction, which is how a player builds *on* a
    // crafting table rather than opening it. Vanilla's rule, and the reason placing
    // is still reachable for every block that also does something.
    if (m_input->isDown(Key::LeftShift)) {
        return false;
    }

    const BlockId block = m_world->blockAt(m_target->block);

    if (block == kCraftingTableBlock) {
        openScreen(ScreenKind::CraftingTable);
        return true;
    }

    if (block == kFurnaceBlock) {
        // **Created on first use rather than when the block is placed.** A furnace
        // that nobody has opened has nothing in it, and building a wall of them
        // should not cost a map entry each. `closeScreen` drops it again if it is
        // still empty when the player walks away.
        m_openFurnace = m_target->block;
        openScreen(ScreenKind::Furnace);
        return true;
    }

    return false;
}

void Engine::tickFurnaces(u32 ticks) {
    if (ticks == 0) {
        return;
    }
    for (auto& [pos, furnace] : m_furnaces) {
        // `tick` reports whether anything moved, which is exactly the question the
        // save needs answered: a furnace burning down is a change to its column that
        // no `setBlock` will ever mark. An idle one marks nothing, so standing next
        // to an empty furnace does not make its column worth writing.
        if (furnace.tick(ticks)) {
            if (Chunk* chunk = m_world->find(toChunkPos(pos))) {
                chunk->markEdited();
            }
        }
    }
}

void Engine::spillFurnace(BlockPos pos) {
    const auto entry = m_furnaces.find(pos);
    if (entry == m_furnaces.end()) {
        return;
    }

    // **Breaking the block is what empties a furnace**, not closing its window. The
    // window closing is the player walking away, and vanilla leaves the contents
    // where they are for that -- which is the whole reason `Container::releaseOne`
    // is virtual and a furnace's returns nothing.
    const vec3 centre{static_cast<f32>(pos.x) + 0.5f, static_cast<f32>(pos.y) + 0.5f,
                      static_cast<f32>(pos.z) + 0.5f};
    for (usize slot = 0; slot < Furnace::kSlots; ++slot) {
        const ItemStack& stack = entry->second.at(slot);
        if (!stack.empty()) {
            m_items.spawn(centre, stack.item, stack.count);
        }
    }

    m_furnaces.erase(entry);
}

void Engine::updateInventoryScreen() {
    const vec2 cursor = cursorNdc();

    const auto aspect = static_cast<f32>(m_window->framebufferWidth())
                      / static_cast<f32>(std::max(1, m_window->framebufferHeight()));
    const ScreenLayout layout{aspect, m_screenKind};

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
        if (left && !m_player.inventory.cursorEmpty()) {
            const ItemStack thrown = m_player.inventory.releaseCursor();
            if (!thrown.empty()) {
                m_items.spawn(m_camera.position(), thrown.item, thrown.count);
            }
        }
        return;
    }

    if (left) {
        m_screen->click(*slot);
    } else {
        m_screen->split(*slot);
    }

    // A click that put ore into a furnace changed the world, and no `setBlock` and
    // no tick will say so -- an unlit furnace with fresh fuel ticks to no effect
    // until something starts it. Marked unconditionally rather than on a comparison:
    // the click may have moved a stack the player's way instead, and writing a
    // column that did not need it costs one 4 KiB record.
    if (m_screenKind == ScreenKind::Furnace) {
        if (Chunk* chunk = m_world->find(toChunkPos(m_openFurnace))) {
            chunk->markEdited();
        }
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
                        [this](ItemId item, u32 count) {
                            const u32 leftover = m_player.inventory.add(item, count);
                            m_itemsCollected += count - leftover;
                            return leftover;
                        });

    if (m_input->wasPressed(Key::E)) {
        toggleInventory();
    }

    if (screenOpen()) {
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
            m_player.hotbarSlot = slot;
            const ItemStack& held = m_player.inventory.at(slot);
            logInfo("Holding: {}",
                    held.empty() ? "nothing" : itemName(held.item));
        }
    }

    // Both are held rather than clicked, on their own timers: breaking runs on the
    // block's hardness, placing on vanilla's fixed 4 ticks.
    updateBreaking(dt);
    updatePlacing(dt);
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
        m_items.spawn(lost.position, itemOfBlock(lost.block), 1);
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
        // **Furnaces burn whether or not anyone is watching**, which is what makes
        // them a simulation rather than a window. One tick each, on the same 20 Hz
        // clock block updates and falling sand run on.
        tickFurnaces(1);

        const BlockUpdates::Stats stats = m_blockUpdates.tick(*m_world, m_falling);
        // **Counted so a play session can see it.** The lesson from 7.14 is that a
        // feature nobody can observe in the log is a feature that ships broken: item
        // pickup was dead for four sessions because nothing printed a number that
        // would have been zero. Water flowing is invisible from a benchmark and hard
        // to be sure of by eye in a cave.
        m_blocksFlowed += stats.flowed;
        m_fluidSuspends += stats.suspended;
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
        //
        // **The top of the highest box, not the top of the cell.** For a cube that is
        // `y + 1` exactly as it always was; for a bottom slab it is `y + 0.5`, which is
        // the whole of what makes standing on one work.
        f32 highest = 0.0f;
        bool found = false;
        for (const BlockBox& shape : blockBoxes(m_world->blockAt(BlockPos{blockX, y, blockZ}))) {
            highest = found ? std::max(highest, shape.topFraction()) : shape.topFraction();
            found = true;
        }
        if (found) {
            return static_cast<f32>(y) + highest;
        }
    }
    return std::nullopt;
}

bool Engine::boxBlocked(const vec3& feet) const {
    const PlayerBox::CellRange range = PlayerBox{feet}.cells();

    // **`blockAt` answers air for a column that is not loaded**, so a player at the
    // edge of the loaded region walks into the unknown rather than into a wall. That
    // is the same call the ground probe already makes -- holding position at an
    // unloaded edge would be a worse lie than passing through it, because the column
    // is usually about to arrive and usually empty where the player is standing.
    const PlayerBox box{feet};
    for (i32 y = range.minY; y <= range.maxY; ++y) {
        for (i32 z = range.minZ; z <= range.maxZ; ++z) {
            for (i32 x = range.minX; x <= range.maxX; ++x) {
                const BlockPos pos{x, y, z};
                // **The boxes, not the cell.** `blockBoxes` is empty for anything not
                // solid -- water is walked into, which is what makes swimming possible
                // at all -- and one full box for a cube, so this is the same test it
                // has always been for every block that fills its cell. A slab is the
                // case that needs the loop.
                for (const BlockBox& shape : blockBoxes(m_world->blockAt(pos))) {
                    if (box.intersectsBox(pos, shape.lowX(), shape.lowY(), shape.lowZ(),
                                          shape.highX(), shape.highY(), shape.highZ())) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
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
    if (!screenOpen()) {
        if (m_input->isDown(Key::W)) { wish += forward; }
        if (m_input->isDown(Key::S)) { wish -= forward; }
        if (m_input->isDown(Key::D)) { wish += right; }
        if (m_input->isDown(Key::A)) { wish -= right; }
    }

    // Read once and used twice: it picks the speed and it turns on the edge-stop.
    // **Sneaking was a speed and nothing else until now**, which meant the key that
    // exists so you can work at the edge of a roof was the key that let you walk off
    // it slowly.
    const bool sneaking = m_input->isDown(Key::LeftShift) && !screenOpen();

    if (math::dot(wish, wish) > 0.0f) {
        const f32 speed = m_input->isDown(Key::LeftControl) ? kSprintSpeed
                          : sneaking                       ? kSneakSpeed
                                                           : kWalkSpeed;
        const vec3 motion = math::normalize(wish) * speed * dt;

        // **This used to ask only how high the ground was at the destination**, which
        // is not a collision test at all: nothing above the feet was ever consulted,
        // so a block at head height with air beneath it was walked straight through --
        // the edge of every tree canopy, every overhang, every block placed one up.
        // And the player was a point, so corners could be cut diagonally.
        //
        // The rules are `slideWithStepUp` now, in `world/WalkMove.hpp`, which is
        // where a test can reach them. What stays here is the only part that needs
        // the engine: what counts as blocked.
        feet = slideWithStepUp(feet, motion.x, motion.z, m_player.onGround,
                               [this](const vec3& box) { return boxBlocked(box); },
                               sneaking);
    }

    // **Jump input is read once**, outside the substep loop below. Reading it per
    // substep would apply the same keypress several times over.
    if (!screenOpen() && m_input->isDown(Key::Space)) {
        if (inWater(feet)) {
            m_player.verticalVelocity = kSwimSpeed;
        } else if (m_player.onGround) {
            m_player.verticalVelocity = kJumpVelocity;
            m_player.onGround = false;
            // **The fall starts here too, and for a long time it did not.** The
            // substep loop below begins tracking when it sees the player was on the
            // ground the step before; the line above clears that flag first, so a
            // fall that began with a jump was never tracked and landed for free from
            // any height. Walking off a ledge tracked correctly, which is what made
            // it read as damage being capped rather than missing.
            m_fall.leftGround(feet.y);
        }
    }

    // **Substepped, and this is the third time in this engine for the same reason.**
    // The ground probe below asks what is solid under the player's *destination*
    // rather than sweeping the path to it, so any step longer than a block passes
    // straight through the floor -- `ItemEntities` and `FallingBlocks` both had this
    // and both were fixed this way. Walking was left with the latent version of it,
    // and an Alt-Tab is what collected: the frame after a hidden window carries the
    // whole absence, gravity reaches terminal velocity inside one step, and the
    // player lands somewhere under the terrain.
    //
    // The frame delta is clamped in the loop as well, which is what actually bounds
    // the stall. This is the structural half: even a legitimately long frame cannot
    // tunnel, because the static_assert in Engine.hpp pins a substep at terminal
    // velocity to less than one block.
    f32 remaining = std::min(dt, kMaxWalkStep * static_cast<f32>(kMaxWalkSubsteps));
    while (remaining > 0.0f) {
        const f32 step = std::min(remaining, kMaxWalkStep);
        remaining -= step;

        // **Swimming, such as it is.** Water holds nothing up, so without this the
        // player sinks to the sea bed at full gravity and walks around down there.
        // Buoyancy is a quarter of gravity with a low sink speed, and Space paddles
        // upward -- which is enough to swim out to an island and back, and is not
        // vanilla's model (no air meter, no drowning, no swimming pose).
        //
        // Re-tested each substep because the feet move within the loop: a step that
        // enters the water has to start floating on the next one, not at the next
        // frame.
        if (inWater(feet)) {
            m_player.verticalVelocity = std::max(
                m_player.verticalVelocity - kGravity * kSwimGravityScale * step, -kSinkSpeed);
            // Entering water cancels a fall, which is vanilla's rule and the reason
            // jumping off a cliff into a lake is a thing people do.
            m_fall.cancel();
        } else {
            m_player.verticalVelocity =
                std::max(m_player.verticalVelocity - kGravity * step, -kTerminalVelocity);
        }

        const f32 beforeY = feet.y;
        feet.y += m_player.verticalVelocity * step;

        // **Heads hit ceilings.** Only on the way up: downward motion is the ground
        // probe's job, and asking the box on the way down would fight it -- the probe
        // deliberately snaps the feet *onto* a surface the box would call an overlap.
        // Without this a jump under an overhang put the player's head through it, and
        // then the ground probe found the floor again and nothing looked wrong.
        if (m_player.verticalVelocity > 0.0f && boxBlocked(feet)) {
            feet.y = beforeY;
            m_player.verticalVelocity = 0.0f;
        }

        const bool wasOnGround = m_player.onGround;

        const auto ground = groundBelow(feet.x, feet.z, feet.y + 0.01f);
        if (!ground.has_value()) {
            // Nothing under us -- almost always a column that has not streamed in
            // yet. Hold height rather than falling through the world while it
            // arrives, which is the same call followGround makes in the benchmark.
            feet.y -= m_player.verticalVelocity * step;
            m_player.verticalVelocity = 0.0f;

            // And do not let that count as a fall. A column arriving late is an
            // engine detail, and taking damage for it would be the game punishing
            // the player for the streaming pipeline.
            m_fall.cancel();
        } else if (feet.y <= *ground) {
            feet.y = *ground;
            m_player.verticalVelocity = 0.0f;
            m_player.onGround = true;

            applyFallDamage(m_fall.landed(feet.y));
        } else {
            m_player.onGround = false;

            // Start measuring from the height we left the ground at, not from the
            // peak. A jump therefore costs its own arc, which is why the three-block
            // grace exists at all -- vanilla measures the same way.
            if (wasOnGround) {
                m_fall.leftGround(feet.y);
            } else {
                m_fall.rose(feet.y);
            }
        }
    }

    m_player.position = feet;
}

void Engine::applyFallDamage(f32 distance) {
    const f32 damage = FallDamage::forDistance(distance);
    if (damage <= 0.0f) {
        return;
    }

    m_player.health = std::max(0.0f, m_player.health - damage);
    logInfo("Fell {:.1f} blocks: -{:.0f} health, {:.0f} left",
            distance, damage, m_player.health);

    if (m_player.health <= 0.0f) {
        respawn();
    }
}

void Engine::respawn() {
    // No death screen and no dropped inventory: both are real vanilla behaviour and
    // both need a decision this has not earned yet -- a screen needs the UI layer to
    // grow a second window, and dropping the inventory needs somewhere for it to go
    // that the player can get back to. Full health where you stand is the honest
    // placeholder, and it says so in the log rather than pretending nothing happened.
    m_player.health = Player::kMaxHealth;
    m_player.verticalVelocity = 0.0f;
    m_fall.cancel();
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
        m_player.position += math::normalize(delta) * speed * dt;
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
    // **Model boxes are counted separately and said out loud, for the reason the
    // block name below is.** A slab is a few hundred pixels in a 1280x720 frame and
    // "did the fourth pass run at all" is not a question a still image answers -- a
    // box that was never submitted and one drawn inside the block next to it look
    // identical from here.
    logInfo("Captured {}x{} frame to {} ({} sections, {} quads and {} model boxes drawn)",
            width, height, m_options.capturePath, stats.sectionsDrawn, stats.quadsDrawn,
            stats.modelBoxesDrawn);

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

    // **The ring's high-water mark is printed because a budget nobody can see is a
    // budget nobody notices overflowing.** `frameRingBytesFor` estimates a worst
    // case; this is what the frames actually used, and `refused` is how many draws
    // the estimate cost. It should stay at zero.
    const rhi::RingLayout& ring = m_frameRing->layout();

    logInfo("{:.1f} FPS ({:.2f} ms) | {} cols | drawn {} sec / {} quads | "
            "culled {} col + {} sec | arena {} MiB | ring {} of {} KiB{}",
            fps, frameMs,
            m_world->loadedChunkCount(),
            stats.sectionsDrawn, stats.quadsDrawn,
            stats.columnsCulled, stats.sectionsCulled,
            m_meshStore->usedBytes() / (1024 * 1024),
            ring.highWaterBytes() / 1024, ring.bytesPerFrame() / 1024,
            ring.refusedCount() > 0 ? std::format(", {} REFUSED", ring.refusedCount())
                                    : std::string{});

    // The interaction half of the line. Separate from the rendering half because it
    // answers a different question -- "did the player do anything" rather than "is
    // the engine keeping up" -- and because three sessions of logs that could answer
    // only the second one is what put it here.
    // Where the player is, and whether they are standing inside the world.
    //
    // **Added because a bug moved the player ten blocks into solid rock and no log
    // line changed.** A stall spent as simulation time dropped the feet through the
    // floor, and the ground probe snapped them to whatever was under the *new*
    // position -- silently, because nothing was tracking a fall and nothing reported
    // a position. `buried` is the tell: feet inside a solid block is never a legal
    // state, so it says "this happened" without needing to know what did it.
    const vec3 feet = playerFeet();
    const bool buried = isSolidBlock(m_world->blockAt(
        BlockPos{static_cast<i32>(std::floor(feet.x)),
                 static_cast<i32>(std::floor(feet.y + 0.5f)),
                 static_cast<i32>(std::floor(feet.z))}));

    // **The save's three counters are on this line for the reason all of these are.**
    // Item pickup was broken through four sessions and a full test suite because
    // nothing printed a figure that would have been zero. A save that has quietly
    // stopped writing looks exactly like a save with nothing to write, and `failed`
    // is the only thing that tells them apart while the game is running.
    const WorldStore::Stats save =
        m_store ? m_store->stats() : WorldStore::Stats{};

    logInfo("  broke {} | placed {} | collected {} | {} items, {} falling, "
            "{} updates queued | flowed {}{} | saved {} / loaded {}{} | "
            "at ({:.1f}, {:.1f}, {:.1f}){}",
            m_blocksBroken, m_blocksPlaced, m_itemsCollected,
            m_items.size(), m_falling.size(), m_blockUpdates.pending(),
            m_blocksFlowed,
            m_fluidSuspends > 0 ? std::format(" ({} suspended)", m_fluidSuspends)
                                : std::string{},
            save.columnsSaved, save.columnsLoaded,
            save.failures > 0 ? std::format(", {} FAILED", save.failures)
                              : std::string{},
            feet.x, feet.y, feet.z, buried ? " BURIED" : "");
}

void Engine::stepFrame(f64 deltaTime) {
    m_renderTime += static_cast<f32>(deltaTime);

    // Return ranges retired long enough ago before allocating any new ones.
    m_meshStore->recycle(m_frame);

    // Before the tick, so a furnace that came off disk this frame burns on this
    // frame's tick rather than sitting cold until the next one.
    adoptLoadedFurnaces();

    // Before submitting, for the same reason the edit path relights before it
    // remeshes: a column that arrives holding a torch has to have the light in it
    // before its sections are handed to a mesher, or it is drawn dark once and only
    // corrected when something else happens to dirty it.
    relightArrivedColumns();

    // Before submitting, so a block broken this frame is remeshed this frame rather
    // than sitting visibly intact until the next one.
    updateInteraction(static_cast<f32>(deltaTime));

    // After interaction and before streaming, for the same reason: a block that
    // falls because of this frame's click should be gone from the mesh this frame.
    updateTicks(static_cast<f32>(deltaTime));

    updateLoadedRegion();

    // Submit only. Nothing here waits for a worker, which is the entire point:
    // whatever the pool has not finished simply appears a frame or two later.
    const usize generated = m_streamer->submitGeneration();
    const usize meshed = m_streamer->submitMeshing();

    if (!m_reportedWarm && generated == 0 && meshed == 0 && m_streamer->idle()) {
        logInfo("Streaming settled: {} columns, {} sections meshed, {} MiB arena",
                m_world->loadedChunkCount(), m_meshStore->sectionCount(),
                m_meshStore->usedBytes() / (1024 * 1024));
        m_reportedWarm = true;
    }

    renderFrame();

    ++m_frame;
    m_streamer->setFrame(m_frame);
}

void Engine::followGround() {
    const vec3 position = m_player.position;
    const i32 blockX = static_cast<i32>(std::floor(position.x));
    const i32 blockZ = static_cast<i32>(std::floor(position.z));

    // Read the world that is already loaded rather than asking the generator. A
    // surfaceHeight() call evaluates a whole column of density, and doing that once a
    // frame would put terrain generation inside the thing being measured.
    for (i32 y = kWorldMaxY - 1; y >= kWorldMinY; --y) {
        if (m_world->blockAt(BlockPos{blockX, y, blockZ}) != kAirBlock) {
            // `kBenchEyeHeight` is where the *eye* flies, and the player holds feet,
            // so the conversion is spelled out rather than the constant being
            // quietly reinterpreted. The camera ends up in exactly the place it was
            // put before, which is what keeps old benchmark numbers comparable.
            m_player.position = {position.x,
                                 static_cast<f32>(y) + kBenchEyeHeight
                                     - PlayerBox::kEyeHeight,
                                 position.z};
            syncCamera();
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
            m_player.position += math::normalize(forward) * kFlySpeed * static_cast<f32>(step);
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
        const f64 elapsed = now - m_lastFrameTime;
        m_lastFrameTime = now;

        // **A stall is not simulation time, and spending it as such throws the player
        // through the floor.** Alt-Tab away and the compositor stops sending frame
        // callbacks, so `swapBuffers` blocks for as long as the window is hidden; the
        // frame that comes back carries the whole absence as one delta. Gravity then
        // integrates seconds of falling in a single step and the ground probe -- which
        // asks what is under the player's *new* position -- never sees the floor it
        // started on. Half a second away is seven blocks down and inside the terrain.
        //
        // The same trade the tick backlog makes in `updateTicks`: the world lags real
        // time by the length of the stall, which is invisible next to the stall.
        const f64 deltaTime = std::min(elapsed, kMaxFrameSeconds);

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

        // **Real elapsed time, not the clamped delta.** The simulation is entitled to
        // ignore a stall; the frame-time report is not, and one that read a steady 60
        // FPS across a window that was hidden for five seconds would be a lie told by
        // the one instrument this project uses to judge a session.
        m_fpsAccumulator += elapsed;
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

    // **Once per frame, before anything writes into it.** This is what moves every
    // renderer off the range the GPU may still be reading; putting it anywhere later
    // would put a write and its draw on opposite sides of a slot change.
    m_frameRing->beginFrame();

    buildVisibleSet();
    m_chunkRenderer->setTime(m_renderTime);
    m_chunkRenderer->draw(*m_device, m_renderCamera, *m_meshStore, *m_frameRing);

    // After the terrain, so the character is depth-tested against a filled buffer
    // rather than against nothing. Only in third person: in first person the model
    // is around the eye and would fill the screen with the inside of a head.
    if (m_thirdPerson) {
        const vec3 feet = playerFeet();
        m_character->draw(*m_device, m_renderCamera, feet, m_camera.forward(),
                          m_walkPhase, m_walkAmount, m_swingPhase, m_swingAmount,
                          heldItem(), m_chunkRenderer->textures(), *m_frameRing);
    }

    m_itemRenderer->draw(*m_device, m_renderCamera, m_chunkRenderer->textures(), m_items,
                         m_falling, m_itemSpin, *m_frameRing);

    // Last, so the outline sits on top of the block it surrounds and of the
    // character if one is in the way. It still depth-tests -- it is inflated just
    // enough to win against its own block's faces and nothing else -- so a wall
    // between the player and the target correctly hides it.
    if (m_target.has_value()) {
        const vec3 origin{static_cast<f32>(m_target->block.x),
                          static_cast<f32>(m_target->block.y),
                          static_cast<f32>(m_target->block.z)};
        const auto aspect = static_cast<f32>(m_window->framebufferWidth())
                            / static_cast<f32>(std::max(1, m_window->framebufferHeight()));

        // **The outline follows the shape, not the cell.** The union of the block's
        // boxes, which is the box itself for everything that has one today; a stair
        // will want the union of two and this already gives it.
        vec3 boxMin{0.0f};
        vec3 boxSize{1.0f};
        const std::span<const BlockBox> boxes = blockBoxes(m_world->blockAt(m_target->block));
        if (!boxes.empty()) {
            vec3 low{boxes[0].lowX(), boxes[0].lowY(), boxes[0].lowZ()};
            vec3 high{boxes[0].highX(), boxes[0].highY(), boxes[0].highZ()};
            for (const BlockBox& box : boxes.subspan(1)) {
                low = math::min(low, vec3{box.lowX(), box.lowY(), box.lowZ()});
                high = math::max(high, vec3{box.highX(), box.highY(), box.highZ()});
            }
            boxMin = low;
            boxSize = high - low;
        }

        m_selection->draw(*m_device, m_renderCamera, origin, aspect, boxMin, boxSize);

        // Only while a break is actually running on *this* block. Progress is reset
        // the moment the crosshair leaves it, so the cracks cannot be left behind on
        // a block the player walked away from.
        if (m_breakProgress > 0.0f && m_breakingBlock.has_value()
            && *m_breakingBlock == m_target->block) {
            m_selection->drawCracks(*m_device, m_renderCamera, origin,
                                    SelectionRenderer::stageFor(m_breakProgress),
                                    boxMin, boxSize);
        }
    }

    // Last of all, because it clears depth. In third person the arm is already on
    // the character and a second one floating in front of the camera would be one
    // arm too many.
    if (!m_thirdPerson) {
        m_character->drawHand(*m_device, m_renderCamera, m_swingPhase, m_swingAmount,
                              heldItem(), m_chunkRenderer->textures(), *m_frameRing);
    }

    // The HUD is genuinely last: it draws over everything, including the hand.
    const auto width = static_cast<f32>(m_window->framebufferWidth());
    const auto height = static_cast<f32>(m_window->framebufferHeight());
    const vec2 cursor = cursorNdc();

    HudRenderer::State hud;
    hud.hotbarSlot = m_player.hotbarSlot;
    hud.health = m_player.health;
    hud.maxHealth = Player::kMaxHealth;
    hud.screen = m_screen.has_value() ? &*m_screen : nullptr;
    hud.screenKind = m_screenKind;

    if (m_screenKind == ScreenKind::Furnace) {
        const auto entry = m_furnaces.find(m_openFurnace);
        if (entry != m_furnaces.end()) {
            hud.cookProgress = entry->second.cookProgress();
            hud.burnProgress = entry->second.burnProgress();
        }
    }
    hud.cursorX = cursor.x;
    hud.cursorY = cursor.y;

    const vec2 aim = aimNdc();
    hud.aimX = aim.x;
    hud.aimY = aim.y;
    if (m_target.has_value()) {
        hud.targetName = kBlocks[m_world->blockAt(m_target->block)].name;
    }

    m_hud->draw(*m_device, m_chunkRenderer->textures(), m_player.inventory, hud,
                width / std::max(1.0f, height), *m_frameRing);
}

void Engine::updateRenderCamera() {
    m_renderCamera = m_camera;
    if (!m_thirdPerson) {
        return;
    }

    // Back along the view direction and **over the right shoulder**, pulled in when
    // terrain is in the way.
    //
    // This used to be an unconditional step backwards, with a note that fixing it
    // needed a voxel raycast the engine did not have. Phase 9 built one for aiming,
    // and this is the second caller: cast from the eye along the displacement and
    // stop short of whatever is hit.
    //
    // **The shoulder offset came out of play.** Straight back put the camera on the
    // view axis, which is where the character is standing -- so the model sat exactly
    // on the crosshair and hid the block being aimed at. A capture from the spawn
    // point shows the selection box completely behind the character's head. The
    // player could not tell whether they were mining wood or stone, which is a
    // reasonable thing not to be able to tell and an unreasonable thing to be unable
    // to see.
    //
    // The offset separates them because the character is nearer the camera than the
    // target is, so it swings further across the screen for the same displacement.
    constexpr f32 kThirdPersonDistance = 4.0f;
    constexpr f32 kShoulderRight = 1.5f;
    constexpr f32 kShoulderUp = 0.4f;
    /// Kept between the camera and the wall, so the near plane does not clip into it
    /// and show the inside of the block.
    constexpr f32 kWallMargin = 0.25f;

    // One cast along the whole displacement rather than one per axis. A lateral
    // offset can put the camera through a wall just as a backward one can, and
    // casting along the direction actually travelled is what covers both at once.
    const vec3 displacement = -m_camera.forward() * kThirdPersonDistance
                              + m_camera.right() * kShoulderRight
                              + Camera::up() * kShoulderUp;

    const f32 reach = std::sqrt(math::dot(displacement, displacement));
    if (reach < 1e-4f) {
        return;
    }
    const vec3 direction = displacement / reach;

    f32 distance = reach;
    if (const auto hit = raycast(*m_world, m_camera.position(), direction, reach)) {
        distance = std::max(0.0f, hit->distance - kWallMargin);
    }

    m_renderCamera.setPosition(m_camera.position() + direction * distance);
}

vec2 Engine::aimNdc() const {
    // Where the aim ray actually lands, in the render camera's screen space.
    //
    // **The crosshair cannot simply sit at the centre of the screen any more.** The
    // ray is cast from the eye, which is where the player is; the frame is drawn from
    // over their shoulder. Those two points do not project to the same place, so a
    // centred crosshair in third person would mark a spot the player is not aiming
    // at -- which is a worse lie than the problem the shoulder offset fixed.
    //
    // In first person the render camera *is* the eye, so this returns the centre and
    // the crosshair does not move.
    const f32 distance = m_target.has_value() ? m_target->distance : kReachDistance;
    const vec3 aimPoint = m_camera.position() + m_camera.forward() * distance;

    const vec4 clip = m_renderCamera.viewProjectionMatrix() * vec4{aimPoint, 1.0f};
    if (clip.w <= 1e-4f) {
        return vec2{0.0f, 0.0f}; // Behind the camera; nothing sensible to mark.
    }
    return vec2{clip.x / clip.w, clip.y / clip.w};
}

} // namespace mc
