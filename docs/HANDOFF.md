# Handoff

Snapshot for resuming work. Written 2026-08-09; updated 2026-08-10 during
Phase 4.

Read `docs/DESIGN.md` for the full design and the reasoning behind every
decision, and `docs/RESEARCH.md` for the vanilla Minecraft numbers the remaining
Phase 4 work is measured against. This file is the short version plus the
practical details needed to pick the work back up cold.

---

## 1. Where things stand

**Phases 0 through 3 are complete. Phase 4 is in progress — 4a, 4b and 4c done.**
Measurements are in DESIGN.md 7.5 (Phase 3), 7.6 (Phase 4) and 7.7 (the benchmark);
vanilla's numbers, and which of them could not be confirmed, are in RESEARCH.md.

**Start here when resuming:** the next piece is lighting, ahead of 4d. Caves exist and
there is no light propagation, so underground is uniformly lit — which is the most
visible thing wrong with the engine today, and more so now that there is ore down there
to look at.

**Do the Quad bit re-layout as part of it.** The two are entangled and the order matters:

- As it stands only bits 57–63 are free, which buys **one flat 4-bit level per quad**.
  Smooth per-corner light needs 4x4 = 16 bits and does not fit.
- `material` is 16 bits for what is now 26 texture layers. Narrowing it to 8 frees
  49–56, and AO plus light can then fold into a single per-corner brightness at 33–48 —
  smooth lighting, quad still 64 bits, 7 bits still spare.
- The cost of that fold is losing `setAoStrength()` as a runtime toggle, since AO stops
  being a separable field.

| Commit | Contents |
|---|---|
| `9c60ddd` | Phase 0 — project skeleton, GL 4.6 context, triangle |
| `207fd7d` | Phase 1 — palette-compressed sections, culled meshing, camera |
| `8180945` | Phase 2 — binary greedy meshing, block texture array |
| `9945f21` | Cleanup pass before Phase 3 (DESIGN.md 7.4) |
| `0d73b2b` | Phase 3a — lock-free MPMC queue and worker pool |
| `1c84b5e` | Phase 3b — chunk columns, world streaming, placeholder generator |
| `7c29082` | Phase 3c — neighbour-aware boundary culling and AO |
| `17bfb20` | Phase 3d/3e — one draw call for the visible set, five-plane frustum |
| `8e60532` | Phase 3f — streaming onto the worker pool; Phase 3 complete |
| `5df5ca6` | Record the Phase 3f commit in the handoff table |
| `111bf53` | Phase 4a — FastNoise2 terrain on a 4x8x4 interpolation grid |
| `d669f31` | Fix the benchmark, which was measuring the wrong thing twice (DESIGN.md 7.7) |
| `c63810e` | Phase 4b — noise caves; README and MIT licence; published publicly |
| `132f29e` | RESEARCH.md — the vanilla block and ore parameters, with sources |
| `e554277` | One block table, so adding a block type is one line |
| `e6c0a3a` | Bedrock and deepslate, and `--probe` to check underground work with |
| `b4faa6e` | Phase 4c — stone variants, gravel and seven ores on one blob feature |
| `dd1438c` | A character, on a second render path (outside the documented scope) |

Working tree is clean. **Published publicly** at the `origin` remote as of
2026-08-10; the earlier local-only rule was lifted by the user at that point.

What runs today: **FastNoise2 terrain with caves and ores** — continents, erosion,
ridged peaks and valleys, a 3D warp for overhangs, cheese caverns on the density grid,
spaghetti and noodle tunnels carved per block, a surface pass that grasses the top of
the terrain (and only the terrain — not cave ceilings), a bedrock floor, a deepslate
band that fades in from Y 8 to Y 0, and blob features placing granite, diorite,
andesite, tuff, gravel and seven ores. **26 block types**, up from five. It streams
infinitely and draws the whole visible set with **one** `glMultiDrawArrays`. Generation
and meshing run on a 6-worker pool, uploads on their own thread, and the main thread
only ever submits.

A character is drawn at the player position on a second render path; `F5` toggles third
person. It is outside the documented scope — see section 6.

| Distance 16 | No caves | Caves | Caves + ores |
|---|---|---|---|
| Frame p99 | 0.85 ms | 6.00 ms | **5.93 ms** |
| Quads drawn | 260 k | 4.1 M | **4.15 M** |
| Arena used | 8 MiB | 112 MiB | **112 MiB** |
| Warm-up, 1,089 columns | — | 2.29 s | **2.99 s** |
| Sections with an empty mesh | 2,509 of 4,967 | **0** | **0** |

**Quote the last column** — caves and ores are both on by default, so 5.93 ms is what the
engine does today. Ores cost essentially nothing to draw (+0.6 % quads, where an ore
block breaks a merge against the rock around it); the 0.7 s of warm-up is the 3x3
feature replay described in section 6. Distance 24 has not been measured since caves
landed and would be close to the budget. The earlier columns are kept only because the
gap between them is the argument for Phase 8.

The last interactive verification — 31 seconds of flying at vsync-locked 60 FPS with no
dropped frames and no GL messages — was taken **before** caves landed, and is now three
phases stale.

No aquifers, biomes or lighting yet. Lighting comes first — see the resume pointer above.

---

## 2. Commands

```bash
# Configure (only needed after CMakeLists changes; deps are cached in .cache/)
cmake --preset debug
cmake --preset release

# Build
cmake --build --preset debug
cmake --build --preset release

# Test  (142 cases, doctest)
ctest --preset debug

# Sanitizers. tsan is mandatory after touching MpmcQueue, JobSystem, or anything
# on the streaming path. See the ASLR note below for why setarch is needed.
ctest --preset asan
setarch $(uname -m) -R ./build/tsan/tests/mc_tests

# tsan over the whole running pipeline, including load/unload while jobs hold pins
TSAN_OPTIONS="suppressions=$PWD/tsan.supp report_mutex_bugs=0" \
  setarch $(uname -m) -R ./build/tsan/src/app/minecraft \
    --render-distance 6 --bench-seconds 12

# Run
./build/debug/src/app/minecraft

# Render one frame headlessly and exit (warms the whole region up first)
./build/release/src/app/minecraft --capture /tmp/shot.ppm
convert /tmp/shot.ppm /tmp/shot.png     # ImageMagick is installed

# Frame-time distribution: vsync off, no cursor capture, camera flies for N REAL
# seconds at 40 blocks/s and follows the terrain. Vsync would make every frame read
# 16.7 ms, so the benchmark turns it off.
#
# Read the "camera flew" and "columns loaded / generating" lines before trusting the
# frame times -- see the note in section 5 about this benchmark lying twice.
./build/release/src/app/minecraft --render-distance 16 --bench-seconds 20

# Re-run the mesher comparison (off by default; it meshes a few hundred times)
./build/release/src/app/minecraft --mesh-benchmark

# What the terrain is actually made of. No GL, no window -- it generates columns and
# counts them. This is the only honest check on anything underground; see section 5.
./build/release/src/app/minecraft --probe --probe-columns 24

# Start in third person, so a capture can show the character (a single frame has
# nobody to press F5).
./build/release/src/app/minecraft --third-person --capture /tmp/shot.ppm
```

**Always measure on the `release` preset.** Debug is `-O0`; timings from it are
meaningless.

Controls: `WASD` move, `Space`/`LeftShift` up/down, `LeftControl` sprint, mouse
look, `F5` toggles third person, `Escape` releases the cursor and then quits.

### Why `--capture` exists

The desktop compositor blocks external screenshot tools — `grim` reports
`wlr-screencopy` unsupported, and X11 `import` cannot see the Wayland window.
The engine reads back its own framebuffer instead. This is also how reference
images get produced for LOD comparison later.

---

## 3. Working rules

These are the user's standing instructions, not suggestions.

1. **Code and documentation in English.** Conversation with the user in Korean.
2. **Do nothing until the user says "승인" (approve).** Discussion and planning
   are the default mode; read-only checks they explicitly ask for are fine.
3. **Ask one question at a time.** Never bundle several open questions.
4. **Minor decisions are yours to make** — the user said so explicitly. Do not
   ask about test frameworks, warning flags, naming, and the like. Decide,
   state what you decided, move on.
5. **Commit when asked.** The repository is public now, so a push is visible
   immediately — still ask before pushing anything not asked for.
6. Stay inside the project directory. The parent directory is off-limits.

---

## 4. Repository map

```
CMakeLists.txt          root; dependencies pinned via CPM
CMakePresets.json       debug / asan / tsan / release / release-tracy
cmake/CompilerWarnings.cmake

src/core/               no dependencies
  Types, Math (only file including glm), Result<T,E>, BitPack,
  Log, Assert, Profile (Tracy macros), Paths, RangeAllocator,
  MpmcQueue (Vyukov bounded lock-free), JobSystem (worker pool)
src/platform/           GLFW lives here and nowhere else
  Window, Input, Clock
src/rhi/                GL abstraction; no GL type in any header
  Device, Buffer, Shader, Texture, VertexArray
src/world/              pure data; knows nothing about rendering
  Coords (+ Face enum, ChunkPosHash), Palette, Section,
  Chunk (12-section column), Neighbourhood (3x3x3 view), World (chunk map)
  BlockTable   — **every block type and texture layer; edit this to add a block**
  BlockRegistry— lookup over that table, and nothing else
src/worldgen/           knows world, nothing above it; FastNoise2 is PRIVATE
  DensityField — the 4x8x4 interpolation grid (no FastNoise2, so it is testable)
  DensityGraph — the noise router; the only file that includes FastNoise2
  Generator    — the pipeline: noise, carvers, surface, features (order matters)
  FeatureTable — the blob features: stone variants, gravel, ores
  Features     — the placer; seamless across columns by replaying the 3x3
  TerrainProbe — --probe; counts what generation produced
src/mesh/               both meshers take a SectionNeighbourhood
  Quad (64-bit packed), ChunkMesh, CulledMesher, BinaryGreedyMesher
src/render/
  Camera, Frustum (5 planes -- no far plane), BlockTextures,
  SectionMeshStore (one persistently mapped arena), ChunkRenderer (one multi-draw),
  CharacterRenderer (the second render path; not voxels)
src/app/
  main, Engine (streaming pipeline: submit-only frame loop, upload thread)

assets/shaders/         chunk.vert, chunk.frag, character.*, triangle.*
tests/                  doctest; links module libraries individually
tsan.supp               third-party race suppressions, with usage in its header
docs/DESIGN.md          the design and the reasoning; measurements in 7.x
docs/HANDOFF.md         this file
docs/RESEARCH.md        vanilla Minecraft block and ore parameters, with sources
README.md               public-facing summary; keep its numbers in step with 7.5-7.7
LICENSE                 MIT
```

One static library per module. **Dependency direction is enforced at link
time**, not just documented: `mc_render` does not link `mc_worldgen`, and glad, GLFW and
FastNoise2 are all linked `PRIVATE` so their types cannot appear in public headers.

---

## 5. Things that will bite you

Learned the hard way; all of them cost real time.

- **`packed` is a reserved keyword in GLSL.** The error points at the assignment
  operator, not at the name.
- **Building a bitmask is not the optimization — never touching the empty cells
  is.** The first greedy mesher was *slower* than the naive one because its
  merge step still walked all 196,608 plane cells to find ~4,800 faces.
- **A GL object cannot be a direct class member** if the class also creates the
  device. Members are initialized before the constructor body loads the GL entry
  points. Use `std::optional<T>` and `emplace()` in the body.
- **Reversed-Z is already set up** — `glClipControl(ZERO_TO_ONE)`, depth cleared
  to 0, `glDepthFunc(GL_GREATER)`, infinite projection with no far plane. Any
  new depth state must respect this or geometry will vanish.
- **The face order in `world/Coords.hpp` is mirrored by three places**: the
  tangent tables in `chunk.vert`, `kPlans` in `BinaryGreedyMesher.cpp`, and
  `kFaces` in `CulledMesher.cpp`. Changing it means changing all four.
- **`-Wconversion` and `-Werror` are on.** Narrowing in bit-packing code is
  almost always a bug, so this is deliberate — expect explicit `static_cast`.
- **A nested struct's default member initializers cannot be seen by a `= {}`
  default argument** in the enclosing class. GCC parses them only after the
  enclosing class is complete, and the error it reports ("could not convert
  `<brace-enclosed initializer list>`") points at the default argument, not at
  the field. This is why `Engine(Options)` takes its argument unconditionally.
- **`Device::clear` takes linear colours, not sRGB** — `GL_FRAMEBUFFER_SRGB` is
  enabled. Use `rhi::srgbToLinear`. See DESIGN.md 6.9 for the whole rule.
- **ThreadSanitizer will not start on this kernel without disabling ASLR.** It
  dies with `FATAL: ThreadSanitizer: unexpected memory mapping`, which looks like
  a bug in the binary and is not — the kernel's `vm.mmap_rnd_bits` is wider than
  TSan's shadow mapping expects. Run it through
  `setarch $(uname -m) -R`. `ctest --preset tsan` hits the same wall, so the
  binary gets run directly.
- **`gl_VertexID` is absolute in OpenGL**, unlike Vulkan's `firstVertex` behaviour:
  it already includes the `first` argument of the draw. That is what lets
  `gl_VertexID / 6` index the shared quad arena with no base offset — and it means
  adding one "helpfully" would break every section but the first.
- **The frustum has five planes.** With an infinite reversed-Z projection the far
  plane's row is `(0, 0, 0, near)`, a zero normal; normalizing it divides by zero and
  the result rejects the entire world. See `render/Frustum.cpp`.
- **A meshing job holds pointers into nine columns across frames.** Unloading one
  underneath it is the sharpest lifetime hazard in the engine. `Chunk::pin()` prevents
  it, and the pin must be held until the *upload* completes, not until the mesher
  returns. `World::updateLoadedRegion` retains pinned columns and counts them in
  `LoadResult::retained`.
- **The upload thread has no GL context and must not need one.** It writes into a
  persistently mapped coherent buffer, which is a memcpy, not a GL call. If anything
  there ever needs a real GL call, that is a design change, not a small fix.
- **TSan over the app reports ~32 races inside GTK** — glib, gio, gobject, fontconfig,
  pango — reached only through `glfwCreateWindow`. Use `tsan.supp`, and read its header
  before adding to it.
- **From above, a world with no caves looks identical to a world full of them.** Tune
  caves against a measured air fraction and a printed cross-section, never a screenshot.
  The cross-section is also what caught the surface rule sanding every cave ceiling.
  This is what `--probe` is for, and it now covers deepslate and ores too — none of
  which are visible from any position a player spawns at either.
- **A measured metric can be wrong in the same direction as the thing it measures.**
  `--probe`'s first underground air fraction counted a fixed Y band, which swept in the
  open sky above low terrain and read 21 % where the honest figure is 6.4 %. It has to
  be air below *each voxel column's own surface*. A metric that agrees with your
  expectations is not evidence that either is right.
- **Vanilla's per-chunk ore counts overshoot badly if taken literally.** Copying them
  gave 42 diamond, 24 lapis and 26 gold per 16x16 chunk against vanilla's rough 4, 4
  and 7 — because vanilla spreads those attempts over a much wider Y range than the
  ore's useful band (diamond is uniform over -80..80), so many land in open air and
  place nothing. Reproduce the count without the waste and every attempt lands in solid
  rock. The counts in `FeatureTable.hpp` are calibrated against measured density and
  carry their scaling factor. Coal, iron and copper needed no correction, which is what
  says the x4 column scaling itself is right.
- **A block type is one line in `world/BlockTable.hpp` and must stay that way.** It used
  to be four edits across three files, two of them index correspondences kept by hand,
  and getting one wrong compiled and ran and put the wrong texture on a block. If you
  find yourself adding a `switch` on BlockId somewhere, add a field to the table
  instead — that is what `glyph` and `stoneLike` are.
- **Thin features cannot live on the interpolation grid.** A 1-5 block tunnel is smaller
  than a 4x8x4 cell, so it has to be carved per block. `DensityGraph::carveThinCaves` is
  the only per-voxel noise in the engine and is bounded three ways; keep it that way.
- **A transposed density grid index looks like a terrain bug, not an indexing bug.**
  FastNoise2's `GenUniformGrid3D` writes x fastest, then y, then z. Get it wrong and
  the world becomes floating horizontal sheets, which sends you looking at the shaper.
  `test_density_field.cpp` pins the layout.
- **FBm cannot make a ridge.** It is symmetric about zero, so more amplitude gives
  taller rolling hills forever. Ridged noise is what puts a crest on a mountain.
- **Tune terrain with the transect probe, not by eye.** Height range, stddev and local
  relief over a few thousand blocks are checkable; "looks about right" from one camera
  angle is not. See DESIGN.md 7.6 for the target numbers.
- **The camera spawn is derived from the surface height.** A constant is wrong the
  moment the shaper changes, and starting inside a hill does not look like a spawn bug.
- **This benchmark has lied twice; check its sanity lines before its results.** It
  reports how far the camera actually flew and how many columns are still generating,
  and warns when the backlog exceeds one region. Both checks exist because of real
  failures: the camera once flew along its view direction and sank out of the bottom of
  the world, and it once advanced by a fixed 1/60 step while frames ran at 5,000 FPS —
  83x real speed, so streaming could never keep up and the visible set emptied. That
  second one biases streaming and rendering in *opposite* directions, which makes the
  result uninterpretable rather than merely pessimistic. DESIGN.md 7.7 has the details.
- **Delta time is easy to get wrong in a way that still runs.** Reading the clock at the
  top of a loop while updating `previous` at the bottom measures the gap *between*
  iterations, not the frame. The first attempt at fixing the above did exactly that and
  moved the camera 11 blocks in 20 seconds.
- CMake needs `LANGUAGES C CXX`; GLFW and glad are C.
- Ninja is not installed; presets use Unix Makefiles.

---

## 6. Phase 4 — in progress

**Goal:** FastNoise2 terrain generation.
**Exit criterion:** infinite terrain traversal.

Sub-steps and measurements are in DESIGN.md 7.6. **4a, 4b and 4c are done.** Minecraft's own
pipeline was researched first, and two findings shaped the plan: the interpolation grid
(see the correction to DESIGN.md 4.1) and the fact that generation is an *ordered*
pipeline — `biomes → noise → surface → carvers → features → light`.

**Next: lighting**, ahead of 4d. Caves exist and light propagation does not, so
underground is uniformly lit, which is the most visible thing wrong with the engine.
Two channels, sky and block, 0–15, `max(sky, block)` at render time.

The bit budget is the first decision and section 1 has it: 7 free bits buy one flat
level per quad, and smooth per-corner light needs `material` narrowed from 16 bits to 8
first. **The storage question is the second, and it is not small.** Two 4-bit channels
over a 32³ section is 32 KiB, which is 384 KiB per column and over 400 MiB at distance
16 — more than the voxels themselves. It needs the same uniform-collapse trick `Palette`
uses, and most sections do qualify: everything above ground is sky 15 throughout and
everything deep is 0. Settle that before writing a propagation loop.

**What 4b actually cost**, now measured rather than predicted — every number here was a
guess in the previous version of this section:

- The fully-enclosed saving is **gone entirely**, not merely reduced: 0 sections produce
  an empty mesh where 2,509 of 4,967 used to. A 6.8% underground air fraction is enough
  to give almost every underground section some cave wall.
- `meshArenaBytesFor` went from 48 KiB per column to **176 KiB**. The old budget predated
  caves and distance 16 wedged against a permanently full arena, which also exposed
  `drainStreaming` looping forever when `store()` kept failing.
- Aquifers were **not** built. Flooded caves with a local water level independent of sea
  level are still open, and need a water block type first.

**4c is done.** Stone variants, gravel and seven ores, all on one `FeaturePlacer`,
because a granite blob and an ore vein are the same operation. Parameters live in
`worldgen/FeatureTable.hpp`, sourced from RESEARCH.md 3 and then calibrated — see the
note in section 5, which is the part worth reading before touching those numbers.

Two things about it are worth knowing before changing anything:

- **Blobs are seamless across columns with no ordering between them.** A vein near a
  border overlaps its neighbour, so generating a column replays the features of all nine
  columns around it and keeps only what lands inside. That works because placement is a
  pure function of (seed, column, feature, attempt) with no sequential RNG state. Keep
  it stateless or the replay stops being cheap and starts being wrong.
- The air-exposure test treats **outside the column as solid**, because the honest
  answer needs a neighbour that may not be generated yet, and asking for it would put an
  ordering dependency between columns that the streaming design exists to avoid. The
  error is confined to blobs touching a border and errs toward placing ore.

Emerald and the badlands gold batch are **deliberately absent**: both are biome-gated,
and shipping them biome-blind would put emerald in every hillside. They are 4d work.

**4d — biomes** from the climate fields that `DensityGraph` already computes. Minecraft
uses a 6-parameter space (temperature, humidity, continentalness, erosion, weirdness,
depth); this engine has three of them today. Emerald and badlands gold land with it.

**4d has an unresolved input.** The wiki publishes only `temperature` and `downfall`
per biome, not the 6-parameter intervals that actually place them, and not a
systematic surface/filler block table. RESEARCH.md 6 records the search that failed;
the game's own worldgen data is where those numbers will have to come from. Settle
that before planning 4d, or the phase starts on a guess.

### The character

`render/CharacterRenderer` draws a blocky humanoid at the player position; `F5` toggles
third person. **It is outside the documented scope** — DESIGN.md ends the project at
terrain generation, and this is the first thing drawn that is not voxels. It is here
because it was asked for.

It reuses the engine's shape of answer rather than its data: no vertex buffer, quads in
an SSBO, six corners from `gl_VertexID`, one draw call. A chunk `Quad` could not be
reused — six-bit lattice integers cannot describe a 0.5-block-wide limb that swings — so
character.vert carries explicit float edges instead. character.frag shares chunk.frag's
face-brightness table exactly, which is what keeps the model in the same light as the
ground rather than reading as a sticker on it.

Known gap, left open on purpose: the third-person camera does not collide with terrain,
so backing into a hill puts the view inside it. Minecraft pulls the camera in on a ray
cast, which needs a voxel raycast this engine does not have.

Phase 5 is indirect draw plus GPU culling. The shader side is already arranged for it:
per-section data is an array indexed by `gl_DrawID`, which means the same thing under
`glMultiDrawElementsIndirect`, so only the command buffer's producer changes.

---

## 7. Decisions already settled

Do not relitigate these without a reason; the rationale is in `DESIGN.md`.

| | |
|---|---|
| Target | render distance 64 chunks at 60 FPS, on an RTX 3060 |
| Rendering | hybrid — raster near, ray marching far (Phase 7) |
| Graphics API | OpenGL 4.6 behind a thin RHI; Vulkan only if profiling proves it |
| Chunk size | 32³ |
| World height | 384, Y from -64 to 320, 12 sections per column |
| Storage | palette compression, 1/2/4/8-bit indices, uniform sections free |
| Meshing | binary greedy, AO-aware near, AO-ignoring for LOD |
| Geometry | no vertex buffers anywhere; quads in an SSBO expanded from `gl_VertexID` |
| Textures | `GL_TEXTURE_2D_ARRAY`, procedurally generated |
| Terrain | density functions on a 4x8x4 interpolation grid, never per voxel — except the thin-cave carve, which cannot be interpolated |
| Errors | exceptions only at init/load boundaries; `Result<T,E>` everywhere else |
| Namespace | flat `mc`, except `mc::rhi` |

## 8. Still open

- **`ChunkRenderer`'s section-origin buffer is overwritten while the GPU may be reading
  it.** `rhi/Buffer.hpp` states the contract — the caller must not overwrite a range the
  GPU may still be reading — and `SectionMeshStore` honours it with `kReuseDelayFrames`.
  `ChunkRenderer::draw` writes offset 0 of a persistently mapped buffer every frame with
  no ring, no fence and no delay. `barrierAfterClientWrites()` orders writes; it does not
  wait for last frame's draw. Vsync at 60 FPS with a 6 ms frame leaves enough slack that
  it has not been observed, which is not the same as it being correct. Phase 5's indirect
  command buffer has the identical problem, so fix both together.
- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- **World persistence** — disk format, and whether it is in scope at all.
- **Water, and the aquifers under it.** Larger than its one line suggests: aquifers run
  *inside* the noise stage on their own grid (RESEARCH.md 4), the mesher only knows the
  `isOpaque` yes/no and would need water-against-water culling, and translucency needs a
  second draw pass. Three changes that have to land together.
- **No test covers `SectionMeshStore`.** It holds the trickiest lifetime logic in the
  engine — deferred reuse, the pending list, arena exhaustion — and `RangeAllocator`
  underneath it is unit-tested while the combination is not.
- **Re-measure AO merging — the precondition is now met.** The 13.5-point figure in
  DESIGN.md 7.3 came from smooth heightmap terrain. Caves and overhangs exist, so
  `--mesh-benchmark` should be re-run against a real generated column rather than the
  synthetic test section it still uses; that is the case that could make AO-aware merging
  a bad trade after all.
- **`.clang-format` and `.clang-tidy` are listed in DESIGN.md 5.2 but do not
  exist.** Adding a formatter now would reformat the whole tree in one commit, so
  it is a deliberate decision rather than a chore — either add them and take that
  commit, or drop them from the document.
