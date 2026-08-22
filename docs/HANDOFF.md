# Handoff

**What is left to do, how to build and run it, where things live, and the traps.**

**Nothing that is already finished goes in this file** — that is DESIGN.md 7.x, and
RESEARCH.md carries the vanilla numbers it was measured against. Keep it compact: a
handoff that reads as a progress report is one nobody can find the next task in.

---

## 1. What to work on

Building, in the order a builder hits it:

| | What | Why here | Size |
|---|---|---|---|
| **1** | **A door** | You cannot close a house without one. Today it is a hole, or a block broken and replaced every time you go in. **Blocked on a decision, below** | medium |
| **2** | **Phase 10 non-cube geometry** | Stairs, slabs, fences, panes — and the torch's real shape (18b: one field flips, `opaque` to false). **This is not a prerequisite, it is the building vocabulary** | large |

**The door is blocked on a decision, not on code.** A vanilla door is 3/16 of a block
thick and two blocks tall; the mesher draws cubes only, and nothing in this engine has
multi-block state — placing sets two blocks and breaking either must break both. So
either a **cube door now** (a second deliberate deviation after the torch, and far more
visible — a door is at eye height in a wall you built), or **Phase 10 first**, which
delivers doors, panes, stairs, slabs, fences and the torch's real shape together
because they are all the same missing thing. The second is the better trade if there is
time for it.

**Building is shelter, not expression.** If that flips, the geometry work moves to the
top and the door stops being the question.

### 1.1 A day/night cycle — in scope, and the cost is the bit budget

**It does not need relighting.** A stored sky light value is *how much sky reaches
here*, a static fact that changes only when a block does; time of day is one scalar
applied at render time. Vanilla keeps a lightmap indexed by `(blockLight, skyLight)`
and regenerates that small texture each tick, and the per-voxel values never move.
`computeSkyLight` is not on this path.

**The cost is the same wall Phase 18 hit.** Quads carry `max(sky, block)` already
combined, at bits 41..56 — four corners of four bits (`mesh/Quad.hpp`). The shader
needs `max(sky * daylight, block)`, so the two have to arrive **separately**: eight
bits a corner where there are four, and the word is full.

| Option | Cost |
|---|---|
| **A parallel light SSBO** — quad unchanged, per-corner sky and block in a second stream indexed the same way | 8 more bytes per quad in its own arena. **Uploaded only when light changes, never when the time does.** Probably the right answer |
| **Widen the quad to 128 bits** | Doubles the mesh arena (187 MiB at distance 16) and the bandwidth. Against DESIGN.md 3.7's whole argument |
| **Spend the AO field** — bits 33..40 | 24 light bits: three sky, three block per corner. Eight levels each, visibly banded, and AO goes |
| **Drop per-corner block light** — one value per quad, three bits of sky per corner | Fits 16 bits exactly. Loses smooth lighting on exactly the light a torch casts |

**Measure before choosing** — the arena figure decides between the first two, and
`--bench-seconds` at full render distance produces it.

Two things a cycle brings that are not lighting: **nothing to do at night** (vanilla's
answer is mobs, Phase 19 — without them the world gets dark and stays safe, which makes
shelter decorative), and **sleeping**, worth having only once night is worth skipping.

Further out: **Phase 19 — mobs, combat, armour** is last and largest, because fall
damage is the only thing in the world that can hurt the player and there is nothing to
fight. **Phase 4d — biomes** is the last of the performance track and is *not* next;
see section 5.

### Play it — this is first on purpose

The list of things this project found by playing is longer than the list it found by
reasoning. Every change of direction came out of a session, and two of the biggest bugs
(item pickup never working; nobody being able to find crafting) survived a full test
suite because no test can answer these:

1. **Does a lit room read as lit, and is a cube torch legible enough to place on
   purpose?** Craft a torch (coal on a stick, in your own 2x2), dig in, and see. This
   is the whole reason 18b exists as a separate item.
2. **Walk the whole chain to a diamond.** `tests/test_progression.cpp` covers the
   tables; what no test reaches is aiming, mining, and finding the windows — which is
   where both previous findings came from.
3. **Build something, quit, come back.** Walk far enough that the column unloads, then
   walk back — that is a different path from quitting, because the save happens at
   unload with workers running. Leave a furnace smelting and walk away. Watch `saved`
   and `loaded` on the stats line. For the player: come back standing where you left
   rather than near it, quit mid-fall and check you do not resume inside the floor,
   and quit with the inventory open holding a stack on the cursor — that last one goes
   through `closeScreen` in `saveEverything` and is the sharpest edge in the save.
4. **Knock a sand pillar over.** Phase 12 has still never been seen by a person. Place
   a few sand blocks and dig out the bottom one. The oldest unverified thing here.
5. **Judge the water.** The ripple amplitude (now 26); whether a surface stepping in
   four levels where vanilla has nine reads as a staircase; whether emptying a bucket
   in mid-air makes a column or a curtain in vanilla (RESEARCH.md 7.1); and `updates
   queued` on a breach — a `Block update queue full` warning is a cascade that does not
   terminate rather than a busy world.
6. **Look at water from underneath, and press `F11` back to windowed.** Back-face
   culling is off for the translucent pass so the surface reads from below, and
   `--capture` cannot put a camera under the sea. The path that restores the remembered
   windowed rectangle has never run. Water is a short walk **west of spawn**.

---

## 2. Commands

```bash
cmake --preset debug            # configure; only after CMakeLists changes
cmake --build --preset debug
ctest --preset debug            # 405 cases, one ctest entry each, 8 at a time
ctest --preset debug -R walk    # a name filter now selects cases, not the binary

# Sanitizers. tsan is mandatory after touching MpmcQueue, JobSystem, or anything on
# the streaming path. If a documented command has not been run in a while, run it
# before trusting it -- asan silently ran a stale binary twice.
ctest --preset asan
setarch $(uname -m) -R ./build/tsan/tests/mc_tests      # ASLR note in section 4

TSAN_OPTIONS="suppressions=$PWD/tsan.supp report_mutex_bugs=0" \
  setarch $(uname -m) -R ./build/tsan/src/app/minecraft \
    --render-distance 6 --bench-seconds 12

./build/debug/src/app/minecraft

# Headless frame. Warms the whole region first.
./build/release/src/app/minecraft --capture /tmp/shot.ppm
convert /tmp/shot.ppm /tmp/shot.png

# Frame times: vsync off, camera flies N REAL seconds at 40 blocks/s.
# Read the "camera flew" and "columns loaded / generating" lines before trusting the
# frame times -- this benchmark has lied twice, once by flying out of the world and
# once by outrunning streaming, which is why it reports both.
./build/release/src/app/minecraft --render-distance 16 --bench-seconds 20

# What the terrain is actually made of. No GL. The only honest check on anything
# underground -- from above, a world with no caves looks identical to one full of them.
./build/release/src/app/minecraft --probe --probe-columns 24

# Stand somewhere, hold something, open a window.
./build/release/src/app/minecraft --at -52 64.2 14 --look 0 -0.12 --capture /tmp/shot.ppm
./build/release/src/app/minecraft --hold wooden_pickaxe --first-person --capture /tmp/shot.ppm
./build/release/src/app/minecraft --furnace --capture /tmp/shot.ppm
./build/release/src/app/minecraft --fly --fullscreen --first-person

# Persistence, checked the only way it can be: twice against the same save.
./build/release/src/app/minecraft --save-path /tmp/w --edit 0 91 0 air --capture /tmp/shot.ppm
```

**Comparing frame times against a baseline**: numbers are not reproducible across
sessions, so a table row from last week is not a baseline. Build the old commit in a
throwaway worktree and interleave the runs A/B/A/B on an idle machine — and put **both
sides through the preset**. The first attempt configured the baseline with a plain
`-DCMAKE_BUILD_TYPE=Release`, which skips the preset's LTO; the new code looked like an
improvement it had nothing to do with.

---

## 3. Repository map

```
src/core/       Types, Math (only file including glm), Result<T,E>, BitPack, Log,
                Assert, Profile (Tracy), Paths, RangeAllocator,
                MpmcQueue (Vyukov bounded lock-free), JobSystem
src/platform/   GLFW lives here and nowhere else: Window (fullscreen, Wayland
                caveats in section 4), Input, Clock
src/rhi/        GL abstraction; no GL type in any header.
                Device, Buffer, Shader, Texture, VertexArray, FrameRing, RingLayout
src/world/      pure data; knows nothing about rendering
  BlockTable    **every block type and texture layer; adding a block is one line here**
  ItemTable     **every item, and the id space that extends BlockId's**; tools,
                mining speed, harvest tiers, drops
  Coords, Palette, LightArray, Section, Chunk (12-section column),
  Neighbourhood (3x3x3 view), World (chunk map + setBlock), BlockRegistry
  SkyLight      daylight, per column, reports what it changed. Its only input is
                `opaque` -- DESIGN.md 7.27 and the guard in setBlock depend on that
  BlockLight    torch flood, **in world space across columns** (15 reach vs 32 column).
                `blockLightCanMove` keeps it off the digging path;
                `noteEmitters`/`hasEmitter` keep it off the streaming path
  Raycast       voxel DDA; aiming and the third-person camera's collision
  ItemEntities  dropped blocks: gravity, merging, despawn. Carries `PickupVolume`
  FallingBlocks sand and gravel between two cells
  BlockUpdates  the tick queue, dedupe, retry discipline, **and the fluid flow**
  Inventory / ItemStack / Container / CraftingGrid / Screen / Crafting / Tools
  PlayerBox     the 0.6-wide collision box; height and eye height live here
  WalkMove      slide-with-step-up, extracted so it can be tested
  RegionFile / WorldStore / ChunkCodec / Furnace   persistence and block entities
src/worldgen/   knows world, nothing above it; FastNoise2 is PRIVATE
  DensityField (4x8x4 grid, no FastNoise2 so it is testable), DensityGraph (the only
  file including FastNoise2), Generator (noise, carvers, surface, features, light --
  order matters), FeatureTable, Features, TerrainProbe
src/mesh/       Quad (64-bit packed), ChunkMesh, CulledMesher, BinaryGreedyMesher
src/render/     Camera, Frustum (5 planes), BlockTextures, SectionMeshStore,
                ChunkRenderer (one multi-draw), CharacterRenderer, SelectionRenderer,
                ItemRenderer, ScreenLayout, HudRenderer, ItemModel
src/app/        main, Engine (streaming pipeline, upload thread, and most else)
assets/shaders/ chunk.*, water.* (reads bits 33..40 as surface height, not AO),
                character.*, selection.*
```

One static library per module. **Dependency direction is enforced at link time**:
`mc_render` does not link `mc_worldgen`, and glad, GLFW and FastNoise2 are `PRIVATE` so
their types cannot appear in public headers.

**`Engine` is 2,477 lines over 58 methods, with 47 members and an 815-line header** —
streaming, player physics, interaction, screens, persistence, furnace ticks,
benchmarks and capture. **It has no tests, and it is the file this project changes
most**: 66 of the first 83 commits touched `Engine.cpp` or `Engine.hpp`. Highest churn
and lowest coverage in the same file is the standing structural risk here.

Anything that can be lifted out of it and tested should be, and the ones already
lifted say what the seam looks like: `WalkMove` (sliding and step-up), `PlayerBox`
(the collision box and the eye height), `Player` (what gets saved), `FallDamage` (the
rule, not the health). **Each was a private constant or a private method that no test
could reach, and two of them were shipping bugs at the time.** Remaining candidates,
roughly in order of how much they would repay: the furnace tick, the streaming
priority function, and `groundBelow`/`boxBlocked`, which are `World` queries wearing
`Engine` as a coat.

---

## 4. Things that will bite you

Learned the hard way; all of them cost real time. The full accounts are in DESIGN.md.

**Adding a block or an item**

- **`BlockInfo`'s fields are positional and new ones go at the *end*.** This has caught
  three bugs. Every entry that spells itself out shifts meaning if a field is inserted.
- **A block type is one line in `BlockTable.hpp` and must stay that way.** If you find
  yourself adding a `switch` on `BlockId`, add a field to the table instead.
- **`seedBlockLight` silently does nothing unless `Chunk::hasEmitter()`.** Writing a
  torch straight into a section is not enough to make it light anything; the writer has
  to call `noteEmitters` first, tests included.

**Rendering and GL**

- **Anything written every frame goes through `rhi::FrameRing`, never its own buffer.**
  Coherence orders writes; it does not wait for last frame's draw. Five renderers had
  this bug and vsync hid all of them.
- **`glBindBufferRange`'s alignment is the driver's choice** — 16 here, 256 elsewhere.
  Query `Buffer::storageOffsetAlignment()`; assuming is a hard GL error.
- **Reversed-Z is already set up** (`ZERO_TO_ONE`, clear to 0, `GL_GREATER`, infinite
  projection). New depth state must respect it or geometry vanishes.
- **The frustum has five planes.** The infinite far plane's row is a zero normal;
  normalizing it rejects the entire world.
- **The face order in `world/Coords.hpp` is mirrored in three other places**: the
  tangent tables in `chunk.vert`, `kPlans` in `BinaryGreedyMesher.cpp`, `kFaces` in
  `CulledMesher.cpp`. Changing it means changing all four.
- **`gl_VertexID` is absolute in OpenGL**, unlike Vulkan — it already includes `first`.
- **`Device::clear` takes linear colours** (`GL_FRAMEBUFFER_SRGB` is on). Has caught
  three pieces of code. Use `rhi::srgbToLinear`.
- **A GL object cannot be a direct class member** if the class also creates the device.
  Use `std::optional<T>` and `emplace()` in the constructor body.
- **A back face samples its texture mirrored unless you say otherwise** — on an
  extruded sprite the tool and its own rim cross in an X. `ItemQuad::mirrorU`.
- **Minecraft's model space is Y-down/Z-back; this engine's is Y-up/Z-forward.**
  Convert published display transforms with a half turn about X applied to the
  *transform* (`C R C^-1`), never with a mirror — negating an axis reverses winding.
- **`glLineWidth` above 1.0 may be silently ignored in a core profile.** A line of a
  chosen width has to be geometry.
- **`packed` is a reserved keyword in GLSL**, and the error points at the `=`.
- **`CharQuad`'s `origin.w` is a texture layer and 0 is valid** — write
  `ItemQuad::kFlatColour` or it draws as stone and says nothing.
- **A fragment shader that *can* `discard` loses early-Z for its whole draw**, taken
  or not. That is the only reason glass has a program and a draw of its own rather
  than an alpha test folded into `chunk.frag`; folding it in would pay the cost on all
  of the terrain in the world. A fourth render layer has the same choice to make.
- **There are three render layers now, in this order: opaque, cutout, translucent.**
  The order *is* the draw order -- opaque fills depth, cutout tests it and may
  discard, translucent blends over both and writes none -- and `ChunkMesh`'s two split
  points, `SectionMeshStore::Placement`'s two counts and `ChunkRenderer`'s three
  origin lists all encode the same ordering. Adding a layer means all three.
- **Light has to be part of the mesher's merge key.** Merging across a light boundary
  stretches one corner's brightness over both faces and draws a hard edge of the wrong
  shade across a cave wall — far more visible than the merge that was lost. The key is
  a mask rather than a shift, because the optional field (AO) is no longer the lowest.

**Terrain generation** — anything touching `Generator`

- **A value computed in one generation stage describes the world as of *that* stage.**
  The sea flood used `terrainTop` from the density field, and the thin-cave carver runs
  after it — so water rested on holes and hung over cave mouths. Generation is an
  ordered pipeline and every cached heightmap in it has this shape of hazard.
- **Vanilla's per-chunk ore counts overshoot badly if taken literally** — copying them
  gave 42 diamond against vanilla's rough 4, because vanilla spreads its attempts over
  a far wider Y range than the ore's useful band and many land in open air placing
  nothing. `FeatureTable.hpp`'s counts are calibrated against measured density and
  carry their scaling factor.
- **Feature placement must stay stateless.** Blobs are seamless by replaying the 3x3 of
  columns around a column and keeping what lands inside, which works only because
  placement is a pure function of (seed, column, feature, attempt) with no sequential
  RNG. The air-exposure test also treats outside the column as solid, deliberately —
  the honest answer needs a neighbour that may not be generated yet.

**Persistence**

- **Never free a region's sectors before the record that replaces them is on disk.**
  Releasing first and reallocating is the obvious way to let a column that grew reuse
  its own gap. It is also how one failed write costs 1024 columns: between the release
  and the failure the header still names those sectors while the map calls them free,
  the next column saved takes them, two entries claim the same sectors, and `open`
  refuses the whole region rather than serve one column's bytes as another's.
  `RegionFile::tryExtend` gets the reuse without the window. A test injects the failed
  write with `RLIMIT_FSIZE`, which is the only way to make a write fail on demand.
- **A stream `flush` is not durability.** It hands the bytes to the kernel and no
  further. `RegionFile::flush` fsyncs, and it is called on eviction and shutdown
  rather than per write — a column is saved on every unload and a disk round trip
  does not belong on the streaming path. The accepted cost is that a power loss can
  leave one column's header naming a record that never landed; `read` rejects it and
  the column regenerates.

**Threading and lifetimes**

- **A meshing job holds pointers into nine columns across frames.** `Chunk::pin()`
  prevents the unload, and the pin is held until the *upload* completes, not until the
  mesher returns. Sharpest lifetime hazard in the engine.
- **The upload thread has no GL context and must not need one.** It is a memcpy into a
  persistently mapped coherent buffer. If it ever needs a GL call, that is a design
  change.
- **Reading a voxel from the main thread is a race unless the column is `Ready`.**
- **`blockAt` answers air for a column that is not loaded**, collapsing "air", "not
  loaded" and "still generating" into one value. Anything reading a *horizontal*
  neighbour to decide an edit must use `World::isReadyAt` and re-queue instead.
- **A block update that gives up leaves the world wrong for ever.** `setBlock` returns
  `Busy` while a meshing job holds the column, and unlike a player's click nothing will
  ask again. Every non-player writer must retry.
- **ThreadSanitizer will not start on this kernel without disabling ASLR.** Run it
  through `setarch $(uname -m) -R`. TSan over the app also reports ~32 races inside GTK
  reached through `glfwCreateWindow` — use `tsan.supp` and read its header first.

**Physics**

- **A collision test that samples the destination instead of sweeping will tunnel, and
  the frame after a stall is when it happens.** This has bitten three times —
  `ItemEntities`, `FallingBlocks`, and walking. All three are substepped and clamped;
  a `static_assert` pins terminal velocity × max substep < 1 block. **A hidden Wayland
  surface gets no frame callbacks**, so the next delta is the whole absence: Alt-Tab
  away and the player came back ten blocks inside the terrain, silently.

**Measuring**

- **A measured metric can be wrong in the same direction as the thing it measures.**
  `--probe`'s first air fraction swept in open sky and read 21 % where the honest
  figure is 6.4 %. A metric that agrees with your expectations is not evidence.
- **`--probe` samples a sparse diagonal, not a neighbourhood.** "water: never placed"
  over 24 scattered columns was read as "no ocean near spawn"; a capture from the spawn
  point shows a lake. **When the question is what the player sees, look at a frame.**
- **A test that walks generated terrain can pass by finding nothing.** The first sea
  test checked its rules against a column entirely above sea level and held vacuously.
- **A well-tested class can have zero coverage of the only thing that is wrong, and the
  seam is where to look.** `ItemEntities` had six passing cases while item pickup had
  never worked once, because every case chose its own radius instead of using
  `kPickupRadius` against the real eye height. **A constant only the caller can see
  cannot be tested** — put geometry next to what it describes. **A named accessor beats
  a subtraction written out five times**: `playerFeet()` exists because
  `m_camera.position() - up * kEyeHeight` appeared at four call sites and the fifth
  passed the camera position instead, which was the whole of that bug. The camera holds
  the *eye*. Worth remembering when the `Player` struct in section 1 gets pulled out.
- **`-Wconversion` and `-Werror` are on, and an incremental build hides breakage in a
  file it did not recompile.** `ctest --preset asan` had been failing to *build* for
  weeks while reporting a pass. CI now catches this class of thing, because a runner
  always starts cold — but only for the two presets it runs.
- **A test that passes is not a test that works.** The way to find out is to break the
  code on purpose and check the test notices. Doing that to `SectionMeshArena::recycle`
  is what proved the reuse-delay cases bite; the first attempt at it compiled with
  errors and *the suite still passed*, against the stale binary, which is the trap
  directly above wearing a different hat. **Check the build succeeded before believing
  the test result.**
- **A concurrent test suite is a different suite.** Running cases 8 at a time surfaced
  a JobSystem case that had been asserting a race it usually won — it counted distinct
  thread ids over 2000 quick jobs, and one worker draining the queue before the others
  wake is a legitimate schedule. It failed about one run in three under load and never
  once on an idle machine. The fix was a rendezvous every worker has to reach, which is
  the stronger claim anyway. **Timing-shaped assertions survive by being lucky.**
- **A nested struct's default member initializers cannot be seen by a `= {}` default
  argument** in the enclosing class. The error points at the default argument, not the
  field. This is why `Engine(Options)` takes its argument unconditionally.

---

## 5. Still open

**Terrain (Phase 4d and beyond)**

- **Biomes** come from the climate fields `DensityGraph` already computes — vanilla
  uses 6 parameters, this engine has 3. **The unresolved input is why this is not
  next**: the wiki publishes only `temperature` and `downfall` per biome, not the
  intervals that place them nor a surface/filler table. RESEARCH.md 6 records the
  search that failed. Settle it before planning 4d, or the phase starts on a guess.
  **Note that 4d moves terrain under existing saves** — old saves keep their edited
  columns and regenerate the rest, so a seam will show near anything built where
  terrain shifted.
- **Sky light does not cross column borders.** A cave lit through an opening one column
  over stays dark, with a straight vertical boundary. Needs a light-changed signal
  threaded into the dirty-mask and pin machinery — a phase, not a patch.
- **Aquifers**, and with them flooded caves and lava lakes. RESEARCH.md 7.2 has the
  thresholds, the 16x40x16 cells and the fluid-level formula; only the *barrier* noise
  is still undocumented anywhere found. Until then, caves under the sea are dry.
- **Trees leave a two-block band along every column edge.** The deliberate cost of
  trees not crossing columns; fixing it properly means a chunk-status pipeline.

**Rendering and performance**

- **Occlusion culling method** — HZB, visibility graph, or both. Decided by profiling
  in Phase 8.
- **Phase 5 inherits the shader layout unchanged.** Per-section data is already indexed
  by `gl_DrawID`, which means the same under `glMultiDrawElementsIndirect`, so only the
  command buffer's producer changes. Reserve a `FrameRing` slice, fill it, bind it.
- **The translucent pass does not sort back to front.** Water gets away with it by
  being the only translucent thing and very nearly flat; a second translucent block
  type is where that stops being true.
- **A sand collapse pays the sky-light recompute twice per block, and it is now the
  only thing that does.** 1.035 ms a time (DESIGN.md 7.27), twice per falling block, a
  dozen over as many ticks for a six-block collapse in open desert. 7.27 took every
  other caller off that path, so **this is the case that would justify the incremental
  relight** `World.hpp` already names.
- **`SectionMeshStore` is now two classes.** The bookkeeping — deferred reuse, the
  pending list, arena exhaustion — is `SectionMeshArena` and is tested; what is left
  in the store is a GL buffer and a memcpy, and that half is still only covered by
  looking at a frame. **The split was forced by the test binary having no GL
  context**, which is the same seam `WalkMove` and `PlayerBox` came out of, and it is
  worth remembering the next time something untestable turns out to be untestable
  only because it is holding a GL object.
- **Re-measure AO merging.** The 13.5-point figure in DESIGN.md 7.3 came from smooth
  heightmap terrain. Caves and overhangs exist now, so `--mesh-benchmark` should run
  against a real generated column rather than the synthetic section it still uses.

**Gameplay**

- **Nothing outside the thirty-six slots is saved.** `Player` is the whole of what
  persists (DESIGN.md 7.28); dropped item entities, falling blocks and the block-update
  queue are all rebuilt or lost. Dropping the inventory on death would need somewhere
  for it to live that is not an entity.
- **No durability**, so a tool never wears out. The field belongs on `ItemStack`.
- **A dropped tool is still a cube with a tool painted on it** — `render/ItemModel.cpp`
  already builds the extruded sprite a *held* tool uses, which is the model a dropped
  one wants. `ItemRenderer` draws one cube per entity from `gl_VertexID`, so this is a
  real change of shape rather than a call swap.
- **A sword is craftable and completely inert.** It exists so the recipe can.
- **Death drops nothing and shows nothing.** Respawn is full health where you stand.
  A death screen needs a window that is not a list of slots — **there is still no
  widget tree and no event routing**, which is fine while every window is a slot list
  and stops being fine at the first one that is not.
- **No lava**, though `fluidLevel` and `fluidSource` are per block type and lava is the
  same algorithm with a step of 2. **Mark it non-opaque**: the sky-light guard in 7.27
  holds because every fluid is, and a test asserts exactly that so a mistake here fails
  loudly rather than quietly costing a millisecond a cell.
- **The water surface steps in four levels where vanilla has nine.** Widening it means
  reinterpreting `material` on fluid quads the way `ao` already is.
- **Water is not mass-conserving in vanilla, and building it as if it were is the
  trap.** A source is never consumed, so a hole in the sea bed floods forever. A
  conservative fluid needs global per-body state, which a chunk-streaming world cannot
  cheaply keep. RESEARCH.md 7.1.
- **The character occludes anything within arm's reach in third person.** The shoulder
  offset separates them at normal range but a block at the player's feet cannot be seen
  past the model. Fading the character when it covers the aim point would fix it
  properly; the block name on the HUD is what covers it today.

**Tooling**

- **CI runs debug and asan on every push** (`.github/workflows/ci.yml`). What it does
  *not* run is release and tsan, both on purpose — release is the only preset that
  turns tests off, and tsan needs `setarch -R` plus a GL context no runner has. **tsan
  is still a command a person runs**, and section 2 still means it.
- **No formatter.** `.clang-format` and `.clang-tidy` were dropped from DESIGN.md 5.2
  rather than added; see the note there for the trade.
