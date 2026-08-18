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
#include "render/HudRenderer.hpp"
#include "render/ItemRenderer.hpp"
#include "render/SectionMeshStore.hpp"
#include "render/SelectionRenderer.hpp"
#include "rhi/Device.hpp"
#include "world/BlockTable.hpp"
#include "world/BlockUpdates.hpp"
#include "world/FallingBlocks.hpp"
#include "world/Inventory.hpp"
#include "world/CraftingGrid.hpp"
#include "world/Furnace.hpp"
#include "world/ItemEntities.hpp"
#include "world/PlayerBox.hpp"
#include "world/Raycast.hpp"
#include "world/Screen.hpp"
#include "world/World.hpp"
#include "worldgen/Generator.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <semaphore>
#include <string>
#include <thread>
#include <unordered_map>
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

        /// Start borderless-fullscreen at the monitor's native resolution. `F11`
        /// toggles it at runtime, and is the way most people will reach it -- the
        /// flag exists so a session can start there without a keypress.
        bool fullscreen = false;

        /// Start in fly mode rather than walking. Walking cannot reach a cave, so
        /// anything inspecting the underground wants this.
        bool flying = false;

        /// Open the inventory window at startup, and seed it with something to look
        /// at.
        ///
        /// **This exists because `--capture` cannot click.** The window is the only
        /// part of the engine that needs a pointer to appear at all, so without a
        /// flag it is the one thing that can never be checked without a compositor
        /// screenshot -- which this desktop does not allow (see the note on
        /// `--capture`). Same purpose as `--fly` and `--first-person`: reach a state
        /// a still frame cannot otherwise be taken of.
        bool openInventory = false;

        /// Open a **furnace's** window rather than a crafting table's, seeded with an
        /// ore and a fuel and already burning. Same argument as `openInventory`: a
        /// furnace is ten seconds of state that `--capture` has no other way to reach,
        /// and its gauges are the part most likely to be drawn wrong.
        bool openFurnace = false;

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

    /// Where the aim ray lands on screen, in NDC. The centre in first person; offset
    /// in third, because the frame is drawn from over the shoulder and the ray is
    /// cast from the eye. This is what the crosshair follows.
    vec2 aimNdc() const;

    /// Points `m_renderCamera` at wherever the frame should be seen from: the
    /// player's eye in first person, a few blocks behind and to the right in third.
    void updateRenderCamera();

    /// Casts the aim ray, applies clicks, and retries edits that lost a race with a
    /// meshing job. One call per frame, before streaming submits anything -- an edit
    /// dirties sections, and doing it after `submitMeshing` would hold the change for
    /// a frame for no reason.
    void updateInteraction(f32 dt);

    /// Advances or abandons the current break. Holding the button on one block for
    /// its full break time is what actually removes it.
    void updateBreaking(f32 dt);

    /// Advances the mining swing, and eases it in and out.
    void updateSwing(f32 dt, bool swinging);

    /// The bottom of the player. The camera holds the *eye*, which is 1.62 up.
    ///
    /// **A named accessor rather than the subtraction spelled out at each use.** It
    /// was written out four times, and the fifth caller -- item pickup -- simply
    /// passed the eye instead and shipped a feature that could never work. Anything
    /// asking "where is the player standing" gets it right by construction now.
    vec3 playerFeet() const;

    /// What is in the selected hotbar slot, or `kNoItem` when it is empty.
    ///
    /// Same argument as `playerFeet` above, one phase later: three separate rules now
    /// depend on what is held -- how fast a block breaks, whether it drops anything,
    /// and whether right-click places -- and they must all be asking about the same
    /// slot.
    ItemId heldItem() const;

    /// Breaks `m_target`, or places the selected block against it. Both go through
    /// `applyEdit`, so both get the same retry behaviour.
    ///
    /// Placing returns whether a block actually went down, which is what lets the
    /// repeat timer charge for a placement rather than for a click: the button is
    /// held across sky, out-of-reach air and the player's own box, and none of those
    /// should cost the next block its promptness.
    void breakTargetBlock();
    bool placeTargetBlock();

    /// Places while the right button is held, on vanilla's 4-tick repeat.
    void updatePlacing(f32 dt);

    /// Edits the world, queueing the edit for the next frame if a job owns the
    /// column. Returns false only when the edit is impossible rather than merely
    /// early.
    bool applyEdit(BlockPos pos, BlockId block);

    /// Runs the fixed-rate half of the simulation: block updates, and whatever else
    /// later has to happen at a rate rather than at a frame.
    ///
    /// **The engine had no game tick before Phase 12.** Everything ran on frame
    /// delta time, which is right for anything a camera watches move and wrong for
    /// anything whose *rate* is part of the behaviour. Vanilla notifies neighbours
    /// 20 times a second, and a cascade that ran at frame rate would collapse a sand
    /// pillar three times faster on a 180 FPS machine than on a 60 FPS one. Flowing
    /// water is on the same clock and will use this unchanged.
    void updateTicks(f32 dt);

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
    std::optional<SelectionRenderer> m_selection;
    std::optional<ItemRenderer> m_itemRenderer;
    std::optional<HudRenderer> m_hud;

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
    /// Vanilla's step height, and the reason a full block has to be jumped.
    ///
    /// This was 1.05 and it felt wrong, because it *was* wrong: Minecraft steps up at
    /// most 0.6 of a block without jumping, which covers slabs and stairs and nothing
    /// else. Stepping a whole block automatically is a mod, not the game. Since every
    /// block in this world is full height, 0.6 means no rise is ever walked up and
    /// every one of them is a jump -- which is exactly how the real thing feels.
    ///
    /// The jump clears it with room to spare: 8.5 m/s against 28 m/s^2 peaks at
    /// v^2/2g = 1.29 blocks.
    static constexpr f32 kStepHeight = 0.6f;

    /// How finely the step-up is probed. Six tries between 0 and `kStepHeight`, which
    /// is enough to find the top of any surface on the block lattice and cheap enough
    /// that it does not matter that it is a search rather than a solve.
    static constexpr f32 kStepProbe = 0.1f;
    /// How far down to look for something to stand on before giving up.
    static constexpr i32 kGroundSearchDepth = 96;

    /// Longest physics step walking takes, and how many of them one frame may run.
    ///
    /// Same discipline and the same numbers as `ItemEntities` and `FallingBlocks`,
    /// because it is the same failure: `groundBelow` samples the destination instead
    /// of sweeping the path, so a step that covers a whole block goes through the
    /// floor. The static_assert at the bottom of this header is what keeps that
    /// impossible; adding speed without reading it is how this comes back.
    static constexpr f32 kMaxWalkStep = 1.0f / 120.0f;
    /// Twelve steps covers the frame clamp below with room to spare.
    static constexpr u32 kMaxWalkSubsteps = 12;

    /// Longest frame the *simulation* is willing to believe in, in seconds.
    ///
    /// **This is the bound that matters, and it exists because of Alt-Tab.** A hidden
    /// Wayland surface gets no frame callbacks, so `swapBuffers` blocks until the
    /// window is visible again and the next frame's delta is the whole absence --
    /// seconds of it. Every consumer of the frame delta then integrates that in one
    /// step. At 0.1 s the worst case is 0.28 blocks of fall, which is imperceptible;
    /// unclamped, half a second is seven blocks and lands the player inside terrain.
    ///
    /// The cost is that the world lags real time by the length of the stall, which is
    /// the same trade `kMaxTicksPerFrame` already makes and is invisible next to the
    /// stall itself. Item despawn timers slow by the same amount, which is arguably
    /// what should happen to a world nobody is looking at.
    static constexpr f64 kMaxFrameSeconds = 0.1;

    // A walking substep at terminal velocity must stay inside one block, or the
    // ground probe can step over the floor it was meant to find. The same assert
    // `FallingBlocks` carries, for the same reason -- read it before raising
    // `kTerminalVelocity` or `kMaxWalkStep`.
    static_assert(kTerminalVelocity * kMaxWalkStep < 1.0f,
                  "a walking substep at terminal velocity must stay inside one block, "
                  "or the ground probe tunnels");

    // And the substep budget has to cover a whole clamped frame, or a long frame
    // silently runs slower than real time rather than merely lagging it.
    static_assert(static_cast<f64>(kMaxWalkStep) * static_cast<f64>(kMaxWalkSubsteps)
                      >= kMaxFrameSeconds,
                  "the walking substep budget must cover a full clamped frame");

    /// Swimming. Not vanilla's model -- there is no air meter, no drowning and no
    /// swimming pose -- but enough that water is somewhere to be rather than a hole
    /// in the world that drops you to the sea bed.
    static constexpr f32 kSwimSpeed = 3.2f;
    static constexpr f32 kSinkSpeed = 1.6f;
    static constexpr f32 kSwimGravityScale = 0.25f;

    /// True when the block at the player's feet is a liquid.
    bool inWater(const vec3& feet) const;

    /// Whether the player's box at `feet` overlaps any solid block.
    ///
    /// **The collision test walking did not have.** The ground probe answers "how high
    /// is the floor here", which says nothing about head height and nothing about
    /// width -- so a block one above the feet, or one the player only clips the corner
    /// of, was walked straight through. `PlayerBox::cells` gives the range and this
    /// asks the world about each cell in it.
    bool boxBlocked(const vec3& feet) const;

    /// The camera the frame is actually drawn from.
    ///
    /// Equal to `m_camera` in first person. In third person it is pulled back along
    /// the view direction, and only this one moves -- `m_camera` stays the player's
    /// eye, because that is what streaming centres on and what the character is
    /// drawn from. Two cameras rather than one moving camera, so "where the player
    /// is" and "where the frame is seen from" cannot drift apart.
    Camera m_renderCamera;

    // ---------------------------------------------------------------------------
    // Interaction (Phase 9).
    // ---------------------------------------------------------------------------

    /// The block the aim ray found this frame, if any. Recomputed every frame rather
    /// than cached across them: the world under the ray changes without the camera
    /// moving, because streaming and the player's own edits both alter it.
    std::optional<RaycastHit> m_target;

    /// An edit that lost a race with a meshing job and is waiting for the pin to
    /// clear. See `World::EditStatus::Busy`.
    struct PendingEdit {
        BlockPos pos{};
        BlockId block = kAirBlock;
        /// Frames spent waiting. An edit that never lands is a leaked pin, and
        /// silently dropping it would hide that; this counts up to a bound and then
        /// complains.
        u32 age = 0;
    };
    std::vector<PendingEdit> m_pendingEdits;

    /// The block currently being mined, and how far through it the player is in
    /// [0, 1).
    ///
    /// Held as a position rather than as a flag on `m_target` because the two can
    /// disagree: looking away mid-swing has to abandon the progress, and coming back
    /// has to start over. Vanilla does the same, and it is the rule that stops a
    /// player chipping four blocks at once by sweeping the crosshair.
    std::optional<BlockPos> m_breakingBlock;
    f32 m_breakProgress = 0.0f;

    /// The mining swing. Phase advances with time while the arm is up; amount eases
    /// in and out so starting or stopping a dig does not pop the limb.
    ///
    /// Driven by time rather than by break progress, deliberately: a swing tied to
    /// progress would run at a different speed for dirt and for deepslate, and would
    /// stop dead on a block that cannot be broken at all.
    f32 m_swingPhase = 0.0f;
    f32 m_swingAmount = 0.0f;

    /// Minecraft's swing is six ticks. Kept because the speed is most of what makes
    /// the motion read as chopping rather than waving.
    static constexpr f32 kSwingPeriod = 0.3f;

    /// Time owed before the held right button places again. Zero means the next
    /// press or the next held frame places immediately.
    f32 m_placeCooldown = 0.0f;

    /// Dropped blocks, and what the player is carrying.
    ItemEntities m_items;
    Inventory m_inventory;

    /// Blocks on their way down, and the queue that decides which ones start.
    FallingBlocks m_falling;
    BlockUpdates m_blockUpdates;

    /// Time owed to the fixed tick. See `updateTicks`.
    f32 m_tickAccumulator = 0.0f;

    /// Minecraft's tick rate, and the clock every block update runs on.
    static constexpr f32 kTickSeconds = 1.0f / 20.0f;
    /// Ticks one frame may run before the remainder is discarded.
    ///
    /// Without a cap, a two-second stall owes forty ticks and spends them all in the
    /// frame after it -- which is a second stall, caused by the first. Dropping the
    /// backlog makes the world lag real time by the length of the stall, which is
    /// the same trade `ItemEntities` and `FallingBlocks` make for their substeps and
    /// is invisible next to the stall itself.
    static constexpr u32 kMaxTicksPerFrame = 4;

    /// Minecraft's `rightClickDelayTimer`: four ticks between placements while the
    /// button is held. Fast enough that a wall goes up by dragging the crosshair,
    /// slow enough that it is still the player choosing where each block goes.
    ///
    /// Declared here rather than beside `m_placeCooldown` because it is derived from
    /// `kTickSeconds`, and a static member's initializer can only name what has
    /// already been declared in the class.
    static constexpr f32 kPlaceIntervalSeconds = 4.0f * kTickSeconds;

    /// Shared spin clock for every dropped item, so a pile turns together.
    f32 m_itemSpin = 0.0f;

    /// How far the player can reach, in blocks. Minecraft is 4.5 in survival and 5
    /// in creative; this has no survival mode to differ from.
    static constexpr f32 kReachDistance = 5.0f;

    /// Frames a pending edit may wait before it is reported as stuck. At 60 FPS this
    /// is a third of a second, which is far longer than a pin should ever be held.
    static constexpr u32 kMaxEditAge = 20;

    /// Which of the inventory's first nine slots is selected.
    ///
    /// **The hotbar used to be a fixed array of nine block types here**, drawn
    /// whether the player held any of them or not. That is what made the bottom of
    /// the screen show nine blocks in an empty world, which is not what vanilla does
    /// and was the first thing anyone said about the HUD. The hotbar is inventory
    /// slots 0-8 now, and an empty slot is empty.
    usize m_hotbarSlot = 0;

    /// The player's own 2x2 crafting grid.
    ///
    /// **Owned by the engine rather than by `Inventory`, and that is Phase 17's whole
    /// structural change.** It was inside the inventory because there was one window
    /// and nowhere else to put it. It is a container now, composed into a `Screen`
    /// alongside the player's slots -- exactly as a crafting table's 3x3 is, which is
    /// why a table needed no new window code.
    CraftingGrid m_playerCraft{2};

    /// Every furnace in the loaded world, by the block it lives in.
    ///
    /// **The first block with state of its own**, and the reason it cannot live in
    /// `Section` with the block id: a furnace is three item stacks and two timers,
    /// where a section stores a palette index per voxel. Vanilla makes the same split
    /// and calls the other half a block entity.
    ///
    /// A map rather than an array because furnaces are rare and their positions are
    /// arbitrary. Entries are created when a player first opens one and dropped again
    /// when it turns out to be `idle()`, so right-clicking every furnace in a village
    /// does not cost anything permanent.
    ///
    /// **This is not saved and does not survive unloading a column.** Persistence is
    /// Phase 11 and this is the first thing that will genuinely need it -- a furnace
    /// that forgets what it was smelting when the player walks away is a defect a
    /// player will notice, and it is recorded rather than hidden.
    std::unordered_map<BlockPos, Furnace, BlockPosHash> m_furnaces;

    /// Advances every furnace by one simulation tick.
    void tickFurnaces(u32 ticks);

    /// Gives a broken furnace's contents back to the world, and forgets it.
    void spillFurnace(BlockPos pos);

    /// Which furnace the open window belongs to. Meaningful only while
    /// `m_screenKind` is `Furnace`.
    BlockPos m_openFurnace{};

    /// The 3x3 of the crafting table currently open, if one is.
    ///
    /// **Created on open and destroyed on close, because a table has no memory in
    /// vanilla either**: what you leave in the grid falls out when you walk away.
    /// A chest is the opposite and is why `Container::releaseOne` is virtual.
    std::optional<CraftingGrid> m_tableCraft;

    /// The open window, or nothing. **One optional rather than a bool plus a kind**,
    /// so there is no state where a screen is open and nobody knows which.
    ///
    /// While it is open the world is not interacted with at all -- no aim ray, no
    /// breaking, no placing, no mouse-look. That is a mode, and modes are worth being
    /// suspicious of, but this one is exactly vanilla's and the alternative is aiming
    /// a crosshair the player cannot see.
    std::optional<Screen> m_screen;
    ScreenKind m_screenKind = ScreenKind::Player;

    bool screenOpen() const noexcept { return m_screen.has_value(); }

    /// Opens the player's own window, or the table's. Closing goes through
    /// `closeScreen`, which is the only place that gives the contents back.
    void openScreen(ScreenKind kind);
    void closeScreen();
    /// `E`: opens the player's window, or closes whatever is open.
    void toggleInventory();

    /// Right-clicking a block that opens rather than being built against. Returns
    /// whether it handled the click, in which case nothing is placed.
    ///
    /// **Vanilla's rule, including the exception**: sneaking suppresses it, which is
    /// how a player puts a block on top of a crafting table rather than opening it.
    bool useTargetBlock();

    /// Resolves a click inside the window to a slot and applies it.
    void updateInventoryScreen();
    /// The pointer in NDC, for hit testing and for drawing the dragged stack.
    vec2 cursorNdc() const;

    // ---------------------------------------------------------------------------
    // Health (Phase 15).
    // ---------------------------------------------------------------------------

    /// In half-hearts, as vanilla counts: 20 is ten full hearts.
    f32 m_health = 20.0f;
    static constexpr f32 kMaxHealth = 20.0f;

    /// Y the player was at when they last left the ground, and whether they are
    /// falling from it. Fall damage is the distance between that and where they land.
    f32 m_fallFromY = 0.0f;
    bool m_trackingFall = false;

    /// Vanilla's rule: no damage for the first three blocks, then one half-heart per
    /// block after that. Three is what makes a jump free and a two-storey drop hurt.
    static constexpr f32 kSafeFallBlocks = 3.0f;

    /// Applies fall damage for a landing from `fromY` to `toY`, and respawns the
    /// player if it kills them.
    void applyFallDamage(f32 fromY, f32 toY);
    void respawn();

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

    /// Interaction counters, for the once-a-second stats line.
    ///
    /// **Three play sessions in a row could not say whether a single block was
    /// broken.** Nothing on the interaction path logged anything -- pickup went in as
    /// `logDebug`, which is off by default and therefore printed nothing -- so the
    /// only evidence those sessions left was which keys were pressed. Four integers
    /// make every future session self-documenting, which matters more in this project
    /// than in most: the last three phases of direction all came out of play rather
    /// than out of reasoning.
    ///
    /// Main thread only. Every one of these is incremented from the frame loop, so
    /// none of them needs to be atomic.
    u64 m_blocksBroken = 0;
    u64 m_blocksPlaced = 0;
    u64 m_itemsCollected = 0;
    /// Water blocks created, moved or removed, and how many spreads had to wait for a
    /// neighbouring column to finish generating.
    ///
    /// Both exist so that "did the water actually flow" is a number rather than an
    /// impression. **The lesson is 7.14's**: item pickup was dead for four play
    /// sessions because nothing printed a figure that would have been zero.
    u64 m_blocksFlowed = 0;
    u64 m_fluidSuspends = 0;
};

} // namespace mc
