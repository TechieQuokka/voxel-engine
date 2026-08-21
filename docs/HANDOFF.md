# Handoff

**What is left to do, how to build and run it, where things live, and the traps.**
Nothing that is already finished — that is DESIGN.md 7.x, and RESEARCH.md carries the
vanilla numbers it was measured against. This file has been cut back to that three
times now; each time it had refilled with descriptions of completed work, which is the
one kind of content a handoff has no use for.

Last cut 2026-08-21.

---

## 1. What to work on

Building, in the order a builder hits it:

| | What | Why here | Size |
|---|---|---|---|
| **1** | **A door** | You cannot close a house without one. Today it is a hole, or a block broken and replaced every time you go in | medium |
| **2** | **Glass** | Windows. Needs a **third mesher pass** — there are exactly two today, opaque and fluid, and a block that is neither emits no faces | medium |
| **3** | **Sneak stops you at an edge** | You fall off your own roof. `kSneakSpeed` changes speed and nothing else; vanilla's edge-stop is missing | **small** |
| **4** | **Phase 10 non-cube geometry** | Stairs, slabs, fences, panes — and the torch's real shape (18b: one field flips, `opaque` to false). **This is not a prerequisite, it is the building vocabulary** | large |

**3 is small and pure profit** — worth doing out of order if a session is short.

Two decisions to know before revisiting any of it:

- **Building is shelter, not expression.** That is what put light first. If it flips to
  expression, item 4 moves to the top and 1-3 matter less.
- **No day/night cycle.** The lighting design rests on sky light being static
  (DESIGN.md 3.7): `max(sky, block)` is exactly right only because outdoors is always
  15. A cycle makes sky light dynamic and means relighting continuously, which is the
  one change that would make the current design expensive. Decide it before building
  on top of the light, not after.

Further out: **Phase 19 — mobs, combat, armour** is last and largest, because fall
damage is the only thing in the world that can hurt the player and there is nothing to
fight. **Phase 4d — biomes** is the last of the performance track and is *not* next;
see section 5.

### Play it — this is first on purpose

The list of things this project found by playing is longer than the list it found by
reasoning. Every change of direction came out of a session, and two of the biggest bugs
(item pickup never working; nobody being able to find crafting) survived a full test
suite because no test can answer these:

1. **Has anyone played block light?** It is built, tested and measured, and whether a
   lit room *reads* as lit — or whether a cube torch is legible enough to place on
   purpose — is unanswered. Craft a torch (coal on a stick, in your own 2x2), dig in,
   and see. This is the whole reason 18b exists as a separate item.
2. **Walk the whole chain to a diamond.** `tests/test_progression.cpp` walks it in the
   tables; what a test cannot reach is aiming, mining, and finding the windows. That
   half is where both previous findings came from.
3. **Build something, quit, come back.** Walk far enough that the column unloads, then
   walk back — that is a different path from quitting, because the save happens at
   unload with workers running. Leave a furnace smelting and walk away. Watch `saved`
   and `loaded` on the stats line. **The player is saved now too**, so check the
   things a test cannot: that you come back standing where you left rather than near
   it, that quitting mid-fall does not resume into the floor, and that quitting with
   the inventory open and a stack on the cursor does not eat the stack — that last one
   goes through `closeScreen` in `saveEverything` and is the sharpest edge in it.
4. **Knock a sand pillar over.** Phase 12 has still never been seen by a person. Place
   a few sand blocks and dig out the bottom one. The oldest unverified thing here.
5. **Judge the water.** Four things a counter cannot answer: the ripple amplitude (now
   26); whether a surface stepping in four levels where vanilla has nine reads as a
   staircase; whether emptying a bucket in mid-air makes a column or a curtain in
   vanilla (RESEARCH.md 7.1); and `updates queued` on a breach — a `Block update queue
   full` warning is a cascade that does not terminate rather than a busy world.
6. **Look at water from underneath, and press `F11` back to windowed.** Back-face
   culling is off for the translucent pass precisely so the surface reads from below,
   and `--capture` cannot put a camera under the sea. Both fullscreen sessions started
   from `--fullscreen`, so the path restoring the remembered windowed rectangle has
   never run. Water is a short walk **west of spawn**.

---

## 2. Commands

```bash
cmake --preset debug            # configure; only after CMakeLists changes
cmake --build --preset debug
ctest --preset debug            # 368 cases, doctest

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

**`Engine` is 3,200 lines and 67 members** — streaming, player physics, interaction,
screens, persistence, furnace ticks, benchmarks and capture. It has no tests. Anything
that can be lifted out of it and tested should be.

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
- **Light has to be part of the mesher's merge key.** Merging across a light boundary
  stretches one corner's brightness over both faces and draws a hard edge of the wrong
  shade across a cave wall — far more visible than the merge that was lost. The key is
  a mask rather than a shift, because the optional field (AO) is no longer the lowest.

**Terrain generation** — the third mesher pass and anything touching `Generator`

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
  weeks while reporting a pass.
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
- **No test covers `SectionMeshStore`** — the trickiest lifetime logic in the engine
  (deferred reuse, pending list, arena exhaustion). `RangeAllocator` under it is
  unit-tested; the combination is not.
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

- **No CI.** The remote is public and nothing is verified on push. The asan and tsan
  presets and `tsan.supp` already exist; a workflow is assembly, not design.
- **368 test cases run as one `ctest` entry**, so `ctest -j` does nothing and a failure
  is not isolated. `doctest_discover_tests()` is the fix.
- **`.clang-format` and `.clang-tidy` are listed in DESIGN.md 5.2 and do not exist.**
  Adding a formatter now reformats the whole tree in one commit — either take that
  commit or drop them from the document.
