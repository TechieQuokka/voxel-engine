# Handoff

Snapshot for resuming work. Written 2026-08-09; last updated 2026-08-12, after
block updates, a real inventory, water, and the item-pickup fix.

Read `docs/DESIGN.md` for the full design and the reasoning behind every
decision, and `docs/RESEARCH.md` for the vanilla Minecraft numbers the remaining
Phase 4 work is measured against. This file is the short version plus the
practical details needed to pick the work back up cold.

---

## 1. Where things stand

**Phases 0-3 complete. Phase 4 in progress** -- 4a, 4b and 4c done; 4d (biomes) is the
last step and is *not* next, because it still has the unresolved input in section 6.
**The whole interaction track is done**: 9, 12, 13, 14 and 15, plus trees, water and
two follow-up batches. The two tracks are independent, so 4d being open alongside a
finished interaction track is not a contradiction.

DESIGN.md 7 has the phase plan; 7.5-7.15 have the results and the reasoning.
RESEARCH.md has the vanilla numbers and records which of them could not be confirmed.

| Built | Write-up |
|---|---|
| Terrain, caves, ores, sky light | DESIGN.md 7.6 |
| Block placement and breaking (9) | 7.8 |
| Trees, per-block break times, the step-height fix | 7.9 |
| Entities, item drops, HUD (13, 14) | 7.10 |
| Block updates and the 20 Hz tick (12) | 7.11 |
| Slot inventory, the UI layer, hearts (15) | 7.12 |
| Water -- oceans and the translucent pass | 7.13 |
| Item pickup, which had never worked | 7.14 |
| The Alt-Tab stall fix, and seeing what you are mining | 7.15 |

**Deliberately not built**, each for a recorded reason: aquifers (so no flooded
caves), flowing water, and the item/block split that crafting forces. Section 8.

### Playing it is what has driven this project

**Every change of direction came out of a play session rather than out of reasoning
about scope.** That is the single most useful thing to know before picking this up.
The lessons are in section 5 and the write-ups in DESIGN.md; this is the index.

| # | Date | What it found |
|---|---|---|
| 1-2 | 08-10 | Twenty-one new block types, all underground, none reachable on foot. Produced the scope change and Phase 9. |
| 3 | 08-11 | The count-based inventory falls short -- a container *is* the feature, not the bookkeeping behind it. Produced 7.12. |
| 4 | 08-12 | `broke 2, collected 0`. **Item pickup had never worked**, found by the stats counters in their first outing. Produced 7.14. |
| 5 | 08-12 | `broke 5, collected 5`. Pickup confirmed. Fall damage fired for the first time. |
| 6 | 08-12 | **Alt-Tab dropped the player through the floor.** Fixed the same day, and the same session then confirmed the fix. First session to reach the water. |

Two of these are worth keeping in full, because both reversed a decision made here.

**Session 3 reversed a recommendation.** Two inventory models were put up: (A) counts
only, one `u32` per block type, no slots and no window; (B) slots with a real screen.
(A) was recommended and chosen, on the grounds that it closes the break-drop-collect-
place loop for a fraction of the work. **The loop argument was right and the conclusion
was wrong** -- carrying things is not felt as a number going up. No amount of reasoning
about scope was going to produce that, and one session did. (B) is what is built now.

**Session 6 confirmed a fix inside the session that reported the bug.** The player
Alt-Tabbed away, came back, and fell through the world; after the fix, the log shows a
7.5 FPS second (a 133 ms frame -- the stall) with the position identical either side of
it and `BURIED` never set. The stall is *visible in the log at all* only because the
frame-time accumulator deliberately uses real elapsed time while the simulation clamps
its delta. Clamping both would have printed a steady 60 FPS and left no evidence.

### Where to resume

**Nothing is half-finished.** The working tree is clean, everything is pushed, and
224 tests, asan and tsan all pass. Pick any of these:

1. **Play it, and knock a sand pillar over.** Phase 12 has *still* never been seen by
   a person: a benchmark flight never edits the world, so it cannot make a block fall.
   Place a few sand blocks from the hotbar and dig out the bottom one. This is the
   oldest unverified thing in the project.
2. **Look at water from underneath.** Back-face culling is off for the translucent
   pass precisely so the surface reads from below, and `--capture` cannot put a camera
   under the sea. Session 6 swam (its Y sat at 60.5-62.9 against a sea level of 62)
   but did not report the view up. The water is a short walk **west of spawn**.
3. **Press `F11` back to windowed.** Both fullscreen sessions started from
   `--fullscreen`, so the path that restores the remembered windowed rectangle -- the
   one with the Wayland caveat under it -- has never run.
4. **Light does not cross column borders.** A cave lit through an opening one column
   over stays dark, with a straight vertical boundary. Needs a light-changed signal
   threaded into the dirty-mask and pin machinery meshing already uses, so it is a
   phase rather than a patch. Section 6 has the shape of it.
5. **The `ChunkRenderer` buffer hazard**, section 8, which Phase 5 will otherwise
   inherit -- and which water has made two writes rather than one.

**Crafting is the next thing that forces a redesign rather than an addition.** Items
are `BlockId`s; a stick is not a block. Everything *else* it needs already exists --
a window, a cursor, a hit test, stack limits -- so a 2x2 grid is a layout change and a
recipe table on top of the item/block split.

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
| `79c4723` | Bring the handoff up to date with 4c, the probe and the character |
| `742a0c6` | Sky light, and the Quad bit re-layout that made smooth lighting fit |
| `5b124e2` | Record the lighting work in the handoff |
| `2b961da` | Walking instead of flying; the character shown by default |
| `a4b1674` | Record the engine-or-game discussion before it could be lost |
| `c504948` | Widen the scope to include interaction, on purpose and in one commit |
| `70b2359` | Phase 9 — voxel raycast, block placement and breaking; you can dig |
| `2275242` | Trees, per-block break time with cracks, and the 0.6 step height fix |
| `a204b83` | Phases 13/14 — mining swing, item drops, inventory, HUD |
| `acdbf07` | Phase 12 — a 20 Hz game tick, block updates, falling sand; stats counters |
| `3dd2f4b` | Slot inventory, the UI layer and window on `E`, hearts and fall damage |
| `83c573c` | Water — oceans, a translucent pass, and the fluid/solid split |
| `af62782` | The fourth play session: item pickup has never worked (section 1) |
| `af8cd6e` | Audit the docs against the code, and fix three stale numbers |
| `240c8a7` | Item pickup measured from the body; the asan build unbroken (7.14) |
| `b8a703e` | Fullscreen on `F11` at the monitor's native resolution |
| `bd87899` | Alt-Tab no longer drops the player through the floor |
| `510f290` | Shoulder camera, thick outline, and the block name under the crosshair |
| `85275bd` | Record the last two commits in the handoff |

Working tree is clean. **Published publicly** at the `origin` remote as of
2026-08-10; the earlier local-only rule was lifted by the user at that point.

What runs today: **FastNoise2 terrain with caves and ores** — continents, erosion,
ridged peaks and valleys, a 3D warp for overhangs, cheese caverns on the density grid,
spaghetti and noodle tunnels carved per block, a surface pass that grasses the top of
the terrain (and only the terrain — not cave ceilings), a bedrock floor, a deepslate
band that fades in from Y 8 to Y 0, blob features placing granite, diorite, andesite,
tuff, gravel and seven ores, and **sky light**, so caves are actually dark.
**30 block types**, up from five -- water is the newest and the only one that is
neither solid nor opaque -- and there are now **trees** on the surface. It streams
infinitely and draws the whole visible set with **one** `glMultiDrawArrays`. Generation
and meshing run on a 6-worker pool, uploads on their own thread, and the main thread
only ever submits.

**The world can be edited, and what you break you keep.** Hold left to break, right
click to place, `1`-`9` pick the block. The arm swings while mining -- on the
character in third person, on a first-person view model otherwise. A wireframe box
shows what is under the crosshair and cracks spread across it as it is mined; break
time is vanilla hardness, 0.75 s for dirt up to 6.75 s for a deepslate ore. Broken
blocks **drop as spinning items**, fall, merge with nearby stacks and can be walked
over to collect -- which is true as of 2026-08-12 and was not before it, see 7.14.
Breaking and placing go through the existing dirty mask, so nothing was added to the
streaming pipeline.

**There is a real inventory**: 36 slots on vanilla's layout, stacks of 64, a window on
`E` with a pointer, click to pick up and put down, right click to split. The hotbar is
its first nine slots, so **an empty slot is empty** -- which is the fix for the nine
grey blocks that used to sit along the bottom of an empty world. **Ten hearts**, and
falling more than three blocks costs some; nothing else damages you yet.

**Sand and gravel fall.** Dig the support out from under a sand pillar and it comes
down one block per tick, as a real falling entity rather than a block stepping down a
cell at a time. That runs on a **20 Hz game tick**, which is new -- everything before
Phase 12 ran on frame delta time. Flowing water will use the same clock unchanged.
Generated sand does not fall until something disturbs it, which is also vanilla.

**There are oceans.** Water fills every column from sea level 62 down to the terrain
surface, drawn translucent in a second pass with depth writes and back-face culling
off. You swim in it rather than standing on it, falling into it cancels fall damage,
sand falls through it and the aim ray goes straight through to the sea bed. **No
flooded caves and no flowing water** -- both are the aquifer system, which is not
built; see section 1.

A character is drawn at the player position on a second render path; `F5` toggles third
person, and the camera now pulls in when terrain is behind it rather than clipping
through — a second use of Phase 9's raycast.

| Distance 16 | No caves | Caves | + ores | + sky light | + editing | + trees | + items | + falling | + water |
|---|---|---|---|---|---|---|---|---|---|
| Frame p99 | 0.85 ms | 6.00 ms | 5.93 ms | 5.91 ms | 6.4–8.0 | 7.55 | 6.20 | 4.40–5.86 | **4.88–5.18** |
| Quads drawn | 260 k | 4.1 M | 4.15 M | 4.18 M | 4.20 M | 4.33 M | 4.33 M | 4.08 M | **4.08 M** |
| Arena used | 8 MiB | 112 MiB | 112 MiB | 113 MiB | 112 MiB | 115 MiB | 115 MiB | 115 MiB | **115 MiB** |
| Warm-up, 1,089 columns | — | 2.29 s | 2.99 s | 3.58 s | 3.40 s | 3.52 s | 3.26 s | 3.29–3.52 s | **3.36–3.47 s** |
| Sections with an empty mesh | 2,509 of 4,967 | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |

**Water is not distinguishable from noise in any of these**, and the reason is worth
knowing rather than being pleased about: an ocean is a flat sheet, greedy meshing
merges a flat sheet into very few quads, and the translucent pass is skipped whole
for every section holding no fluid — everything above sea level and everything below
the sea bed. A capture from the spawn point draws 0.27 % more quads with every ocean
in the render distance included. The translucent surface that would cost something is
one that is not flat.

**The last column is three runs on a deliberately idle machine, and that is a change
of method rather than of code.** Earlier in the same session, with an asan build
occupying the other cores, the same binary reported a 4.02 s warm-up against the
3.29–3.52 s it gives when nothing else is running — a ~20 % swing from background
load alone. **No column before this one controlled for that**, so comparing them at
a resolution of one millisecond is not sound, and the p99 dropping below 7.10's 6.20
is much more likely to be the quiet machine than anything in Phase 12. Nothing in
Phase 12 touches the render path. The quad count differs because the flight ends
somewhere slightly different each time, not because geometry changed.

**And none of this measures the feature.** A benchmark flight never edits the world,
so nothing is ever notified and no block ever falls. These numbers say an idle tick
loop is free. What a sand collapse costs — chiefly the sky-light recompute, twice per
block — has not been measured, and needs a person to cause one.

**Read the last column carefully; it is the one place in this document where a number
got worse and the honest answer is "cannot tell".** The p99 figure is a *range over
three consecutive runs* (6.37, 6.94, 7.98), and the mean over the same runs ranged
3.92 to 4.78. Every earlier column in that row is a single run. A spread of 1.6 ms
between identical runs is far wider than one 5-block DDA march plus one 24-vertex draw
call could plausibly cost, so **do not read this as a regression, and do not read it as
proof of no regression either** — it says the benchmark cannot resolve a change this
small on this machine. Settling it means running the same binary several times before
and after, which has not been done.

Warm-up moved twice and both moves are understood. It *improved* 3.58 s to 3.40 s
when light learned to detect changes -- the store pass stopped clearing each mixed
section and re-expanding the nibble array on the first write, and comparing in place
allocates nothing. It then went back up to 3.52 s when trees landed, which is the
0.12 s they cost to place. Trees add 3 % to the quad count and 3 MiB of arena: leaves
are opaque cubes that fragment a merge wherever they meet air, which is everywhere
along a canopy.

Neither ores nor light cost anything measurable to *draw*: ores add 0.6 % quads and
light 0.7 %, both because they fragment a merge only where they change. Both cost
generation time instead. Distance 24 has not been measured since caves landed and
would be close to the budget. The earlier columns are kept only because the gap
between them is the argument for Phase 8.

Sky light costs **24 KiB per column**, about 26 MiB at distance 16, because 87.5 % of
sections are uniform and allocate nothing. The naive figure was over 400 MiB.

**Interactively verified in six play sessions**, most recently on 2026-08-12 in
fullscreen at 2560x1440. Sessions have run 56-111 seconds at a vsync-locked 60 FPS,
with no dropped frames, no GL debug messages and a clean exit every time. Rendering
has stayed flat across caves, ores, light, trees, entities, the HUD and water.

**What no session has exercised is Phase 12.** Falling sand has never been seen by a
person and a benchmark cannot see it -- the flight never edits the world, so nothing
is notified and nothing falls. The p99 above measures an idle tick loop costing
nothing. What a sand collapse costs, chiefly the sky-light recompute twice per block,
is unmeasured. See the resume pointer in section 1.

---

## 2. Commands

```bash
# Configure (only needed after CMakeLists changes; deps are cached in .cache/)
cmake --preset debug
cmake --preset release

# Build
cmake --build --preset debug
cmake --build --preset release

# Test  (224 cases, doctest)
ctest --preset debug

# Sanitizers. tsan is mandatory after touching MpmcQueue, JobSystem, or anything
# on the streaming path. See the ASLR note below for why setarch is needed.
#
# The asan preset failed to *build* from the HUD landing until 2026-08-12, and nothing
# noticed because the debug tree never recompiled the offending file. Run these after
# a long gap before trusting them -- see the -Wsign-conversion note in section 5.
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

# Start flying rather than walking. No longer the only way underground -- digging
# works now -- but still the fastest way to go and look at something specific.
./build/release/src/app/minecraft --fly

# First person, if the character is in the way of what is being looked at.
./build/release/src/app/minecraft --first-person --capture /tmp/shot.ppm

# Start fullscreen. `F11` toggles it at runtime; this is for starting there, and it
# is also how a capture at the monitor's native resolution is taken.
./build/release/src/app/minecraft --fullscreen --capture /tmp/shot.ppm

# Open the inventory and seed it, then capture. The window is the only thing in the
# engine that needs a pointer to exist, so --capture cannot otherwise reach it.
./build/release/src/app/minecraft --inventory --capture /tmp/shot.ppm
```

**Always measure on the `release` preset.** Debug is `-O0`; timings from it are
meaningless.

**Controls.** Walking and third person are the defaults.

| Key | Walking (default) | Flying (`F`) |
|---|---|---|
| `WASD` | walk, 4.317 blocks/s | move along the view direction |
| `LeftControl` | sprint, 5.612 | 4x speed |
| `LeftShift` | sneak, 1.3 | descend |
| `Space` | jump, clears one block | ascend |
| `F` | switch to flying | switch to walking |
| `F5` | first person / third person | same |
| `F11` | **fullscreen at the monitor's native resolution** | same |

Third person is **over the right shoulder**, and the crosshair follows the aim ray
rather than sitting at the screen centre -- in third person those are not the same
point. The block under the crosshair is **named on the HUD**, which is the only thing
that works when the character is standing in front of what is being mined.
| `Escape` | release cursor, then quit | same |
| **Left mouse** | **hold to break the highlighted block** | same |
| **Right mouse** | **place the held block against it** | same |
| **`1`-`9`** | **pick which hotbar slot to place from** | same |
| **`E`** | **open and close the inventory** | same |
| walk over an item | **pick it up** | same |

Reach is 5 blocks. **Breaking is held, not clicked** -- cracks spread across the block
while the button is down, and the time is vanilla's hardness: 0.75 s for dirt, 2.25 s
for stone, 4.5 s for an ore, 6.75 s for one in deepslate. Letting go or looking away
abandons the progress, which is vanilla's rule and what stops a player chipping four
blocks at once by sweeping the crosshair. Placing stays a single click.

A left click with the cursor released re-captures it instead of breaking anything, so
`Escape` then click does not dig a hole.

**Bedrock cannot be broken** — it is the world's floor, and vanilla refuses for the
same reason.

**Sand and gravel fall when you dig under them**, one block per 20 Hz tick, which is
the fastest way to see Phase 12 without going looking for a desert: place a few sand
blocks in a stack from the hotbar and knock the bottom one out. Placing sand in
mid-air also works — it falls on the next tick, exactly as vanilla does.

**Walking can now reach a cave: dig down.** That is the point of Phase 9, and it is
the first time the ores, deepslate and sky light of the previous three phases are
reachable without `--fly`.

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
  Window (also fullscreen: monitor selection, the Wayland caveats in section 5),
  Input, Clock
src/rhi/                GL abstraction; no GL type in any header
  Device, Buffer, Shader, Texture, VertexArray
src/world/              pure data; knows nothing about rendering
  Coords (+ Face enum, ChunkPosHash), Palette, LightArray, Section,
  Chunk (12-section column), Neighbourhood (3x3x3 view), World (chunk map + setBlock)
  BlockTable   — **every block type and texture layer; edit this to add a block**
  BlockRegistry— lookup over that table, and nothing else
  SkyLight     — the daylight flood fill, per column; reports what it changed
  Raycast      — voxel DDA; aiming, and the third-person camera's collision
  ItemEntities — dropped blocks: gravity, merging, despawn. The first non-voxel
                 thing that exists in the world. Carries `PickupVolume`, which is
                 measured from the player's **body** -- see the note in section 5
  FallingBlocks— sand and gravel between two cells. Straight down, i32 x and z
  BlockUpdates — "this block changed, tell its neighbours": the tick queue, the
                 dedupe set and the retry discipline. **Where water plugs in**
  Inventory    — 36 slots, stack limits, and the cursor stack the window drags
  BlockTable also carries **fluid**; `isSolidBlock` is the test physics wants
  BlockTable also carries **hardness**, **drops** and **falls**
src/worldgen/           knows world, nothing above it; FastNoise2 is PRIVATE
  DensityField — the 4x8x4 interpolation grid (no FastNoise2, so it is testable)
  DensityGraph — the noise router; the only file that includes FastNoise2
  Generator    — the pipeline: noise, carvers, surface, features, light (order matters)
  FeatureTable — the blob features (stone variants, gravel, ores) and the trees
  Features     — the placer; blobs are seamless by replaying the 3x3, trees are
                 inset instead and cannot cross a column -- see TreeSpec
  TerrainProbe — --probe; counts what generation produced, block and light
src/mesh/               both meshers take a SectionNeighbourhood
  Quad (64-bit packed), ChunkMesh, CulledMesher, BinaryGreedyMesher
src/render/
  Camera, Frustum (5 planes -- no far plane), BlockTextures,
  SectionMeshStore (one persistently mapped arena), ChunkRenderer (one multi-draw),
  CharacterRenderer (the second render path; not voxels),
  SelectionRenderer (the block outline and the breaking cracks; no buffer at all),
  ItemRenderer (every dropped item *and every falling block* in one draw call),
  InventoryLayout (every slot rectangle; the renderer AND the hit test use it,
                   which is the whole reason it exists),
  HudRenderer (crosshair, hotbar, hearts, the inventory window — a small UI layer)
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
  enabled. Use `rhi::srgbToLinear`. See DESIGN.md 6.9 for the whole rule. **This has
  now caught three separate pieces of code**, most recently the crack overlay, whose
  "near-black" `12/255` came out mid-grey because that value is linear and encodes to
  about `0.24` sRGB. Anything writing a colour constant straight into a fragment
  output or a non-sRGB texture has to be given the *linear* number: `2/255` was what
  the cracks actually wanted.
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
- **A flood fill seeded everywhere is not a flood fill, it is a scan.** The first sky
  light pass pushed every daylit cell into the queue -- a quarter of a million per
  column, nearly all of them surrounded by cells already at full brightness with
  nothing to give. Seeding only the cells in the step between a column's terrain height
  and a taller neighbour's is exactly equivalent and is the difference between
  affordable and not.
- **Light has to be part of the mesher's merge key.** Merging across a light boundary
  stretches one corner's brightness over both faces and draws a hard edge of the wrong
  shade across a cave wall — much more visible than the merge that was lost. The key is
  a mask rather than a shift now, because the optional field (AO) is no longer the
  lowest one.
- **A block type is one line in `world/BlockTable.hpp` and must stay that way.** It used
  to be four edits across three files, two of them index correspondences kept by hand,
  and getting one wrong compiled and ran and put the wrong texture on a block. If you
  find yourself adding a `switch` on BlockId somewhere, add a field to the table
  instead — that is what `glyph` and `stoneLike` are.
- **A collision test that samples the destination instead of sweeping the path will
  tunnel, and the frame after a stall is when it happens.** `ItemEntities` asks "is
  the block I am moving into solid", which is fine at 1/60 and wrong at any step
  longer than a block -- a window drag, a breakpoint, the first frame. A test that
  aged an item by 299 seconds in one tick found it falling out of the world instead.
  Physics is substepped and clamped now; ageing still uses the real elapsed time.
  Walking's ground probe has the same shape and the same latent issue.
- **A value computed in one generation stage describes the world as of *that* stage.**
  The sea flood used `terrainTop`, which comes from the density field -- and the thin
  cave carver runs after it. Where a cave broke through the sea bed the flood rested
  its lowest water block on a hole, leaving water hanging over a cave mouth. Thirteen
  of them in three columns, found by a test that counted rather than by looking at
  the world. Generation is an ordered pipeline (DESIGN.md 7.6) and every cached
  heightmap in it has this shape of hazard.
- **A test that walks generated terrain can pass by finding nothing.** The first
  version of the sea test checked its three rules against the origin column, which
  for that seed is entirely above sea level: every rule held vacuously. It searches
  outward for an ocean now and requires having found one. Any test whose subject is
  "what the generator produced" needs that guard.
- **`blockAt` answers air for a column that is not loaded, and "air below me" is what
  makes a block fall.** Those two facts together are a bug waiting to happen: sand at
  the edge of the loaded region dropping into a column that has simply not arrived
  yet. `BlockUpdates::examine` is safe from it by a *narrow* argument -- the block it
  asks about is directly below, so it is in the same column, and if that column were
  not Ready the block being examined would have read as air too. **Flowing water
  spreads sideways and the argument does not survive it.** Anything that reads a
  horizontal neighbour to decide an edit needs a real "is this loaded" test.
- **A block update that gives up leaves the world wrong for ever.** `setBlock` returns
  `Busy` while a meshing job holds the column, and unlike a player's click -- which the
  player will simply repeat -- nothing will ever ask about that block again. Both new
  writers retry: the update re-queues itself, and a falling block that cannot land
  holds position rather than being swept, because the entity is the only copy of that
  block that exists.
- **A physics test that samples the destination will tunnel, and this is the second
  time.** `FallingBlocks` has the same shape of landing test `ItemEntities` does and
  therefore the same failure at long delta times. It is substepped for the same
  reason, and a `static_assert` pins the relationship that makes it safe: terminal
  velocity times the maximum substep must stay under one block. Adding speed without
  reading that assert is how it comes back.
- **A well-tested class can have zero coverage of the only thing that is wrong, and
  the seam is where to look.** `ItemEntities` had six cases covering gravity, merging,
  despawn, column unload, falling out of the world and a full inventory — and item
  pickup had never worked once, for four play sessions. Every one of those cases called
  `collect(position, radius)` with a position and radius **of its own choosing**, so
  none of them asserted anything about `Engine::kPickupRadius` against
  `CharacterRenderer::kEyeHeight`. The bug was in the relationship between two
  constants in two modules, combined at one call site, and neither module was wrong.
  A test that picks its own inputs is testing the unit; the seam needs the *real*
  constants, which usually means moving one of them somewhere a test can reach.
- **A constant that only the caller can see cannot be tested, so put geometry next to
  what it describes.** `kPickupRadius` was private in `Engine`. Moving it to
  `ItemEntities` was most of what made the fix above verifiable, and it is a rule
  worth applying before the next number like it goes in.
- **A named accessor beats a subtraction written out five times.** `playerFeet()`
  exists because `m_camera.position() - up * kEyeHeight` appeared at four call sites
  and the fifth caller passed the camera position instead — which is the whole of the
  pickup bug. The camera holds the *eye*; anything asking where the player is standing
  has to convert, and now cannot forget to.
- **`-Wsign-conversion` on a `u16` bitmask is a from-scratch-build failure that an
  incremental build hides.** `HudRenderer.cpp`'s digit blitter did
  `kDigits[glyph] >> bit & 1u` on a `std::array<u16, 10>`: the element promotes to
  `int`, and `& 1u` converts it back. It went unnoticed because the debug tree never
  recompiled that file after it landed, and only surfaced when the asan preset — which
  had a build directory older than the HUD — compiled it fresh. **`ctest --preset asan`
  had been failing to build since the HUD landed.** If a documented command has not
  been run in a while, run it before trusting it.
- **Wayland does not tell a client where its own window is, and GLFW reports that as
  an error.** `glfwGetWindowPos` logs `65548: the platform does not provide the window
  position` on every call. `Window::hasWindowPosition()` guards each one; the
  fullscreen path falls back to the primary monitor, which is also the platform's own
  answer since the compositor decides where a fullscreen surface goes.
- **A Wayland resize is a round trip, so the new framebuffer size does not exist on
  the line after the call that asked for it.** `glfwSetWindowMonitor` followed
  immediately by `glfwGetFramebufferSize` returns the *old* size, and a tight
  `glfwPollEvents` loop spins through its whole budget before the compositor can
  reply -- it has to be `glfwWaitEventsTimeout`. Interactively this is invisible
  because the next frame corrects it; `--capture --fullscreen` has no next frame and
  wrote a 1280x720 image of a 2560x1440 window until `setFullscreen` learned to wait.
- **`--probe` samples a sparse diagonal, not a neighbourhood, and reading it as
  coverage is a mistake that has now been made.** It walks `ChunkPos{i*3, i*5}`, so 24
  columns are 24 scattered samples strung out to chunk (72, 120) -- deliberately, so
  continentalness varies across them. "water: never placed" over that set was read as
  "there is no ocean near spawn" and used to answer a question about what the player
  could see. A capture from the spawn point shows a lake with a sand shore. **When the
  question is what the player sees, look at a frame.**
- **A stall is not simulation time, and the frame after one is where that bites.**
  A hidden Wayland surface gets no frame callbacks, so `swapBuffers` blocks until it
  is visible again and the next delta is the whole absence. Unclamped, gravity spent
  it in one step and the ground probe -- which samples the destination -- never saw
  the floor: **Alt-Tab away for half a second and the player came back ten blocks
  inside the terrain.** Silently, because `m_trackingFall` is false in that path, so
  no fall damage fired and no number in the log changed. `kMaxFrameSeconds` bounds it
  and walking is substepped like `ItemEntities` and `FallingBlocks`; the stats line
  carries a position and a `BURIED` flag now, because the bug moved the player into
  rock without changing anything that was being printed.
- **`glLineWidth` above 1.0 may be silently ignored in a core profile**, which is why
  the selection outline was one pixel for as long as it was. A line of a chosen width
  has to be built as geometry -- `selection.vert` expands each cube edge into a
  screen-space quad. `rhi::Device::drawLines` is unused now and its header says why.
- **Reading a voxel from the main thread is a race unless the column is `Ready`.**
  `World::blockAt` looked like a pure lookup and was called every frame by walking's
  ground probe and the benchmark camera long before anything else used it. It is not
  pure: a column in `Generating` is being written by a worker, and `Palette::fill`
  *frees the index vector*, so a reader can walk memory that has just gone back to
  the allocator. Phase 9's aim ray made it fire under tsan; the two older callers had
  the same bug and had simply never been caught. The fix is one state test inside
  `blockAt`, which is why every caller gets it for free -- but a *new* voxel reader
  that goes around `blockAt` to `Section` directly reintroduces it.
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

**Lighting is done, and it landed better than this section predicted.** The prediction
was that AO and light would have to fold into one per-corner brightness, costing
`setAoStrength()`. Moving `material` to the top of the word instead — seven bits, for a
table of 26 layers — freed 41..56 for four 4-bit corners of light with AO untouched at
33..40. Smooth lighting, quad still 64 bits, AO still separable, no bits wasted.

Storage was the question that mattered and `LightArray` answers it the way `Palette`
does: 87.5 % of sections are uniform and allocate nothing, so a channel that would cost
over 400 MiB at distance 16 costs 26.

**Sky only.** Block light is the same propagation over a second array, but nothing in
the world emits light — no torches, and lava is not a block type — so it would be a
uniformly zero array everywhere.

**The seam is the thing to fix next.** Propagation is column-local, so a cave lit
through an opening one column over stays dark, with a straight vertical boundary. The
vertical fill is exact and depends on nothing but the column's own heightmap, so open
sky and the surface are unaffected; the error is confined to cave interiors within
about fifteen blocks of a border. Fixing it means propagating between columns once
neighbours are loaded, and then re-meshing what changed — which is a light-changed
signal into the same dirty-mask and pin machinery meshing already uses. Getting that
wrong corrupts meshes rather than merely dimming them, so it is a phase of its own.

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

### Phase 9 — block placement and breaking (done)

Full write-up in DESIGN.md 7.8. The three things worth knowing before touching it:

- **Editing is safe without a lock because of the pin, and only because of it.**
  `World::setBlock` refuses with `Busy` when the column is pinned, and the caller
  retries next frame. Every reader of a column pins it — a job meshing a *neighbouring*
  section pins all nine of its columns — so one `pinned()` test covers every reader
  there can be. Take that test out and the failure is a use-after-free, because
  `Palette::set` can reallocate the index array.
- **An edit dirties more than its own section, and the third reason is the subtle
  one.** The section holding the block; every section the block *touches*, because AO
  reads a 3x3x3 and a corner block reaches seven neighbours across column boundaries;
  and wherever the sky light moved, which also dirties the same section in the eight
  surrounding columns because the mesher's padded light grid reaches into them. That
  last one is why `computeSkyLight` now returns a changed-sections mask — without it
  every click would remesh nine columns, and underground, where digging happens, the
  honest answer is that no light moved at all.
- **Relight is a whole-column recompute, deliberately.** About 0.5 ms, on a per-click
  path rather than a per-frame one. Incremental relighting is the optimisation to
  reach for if it ever appears in a profile, and not before.

### The character

`render/CharacterRenderer` draws a blocky humanoid at the player position; `F5` toggles
third person. It was outside the documented scope when it landed — DESIGN.md ended the
project at terrain generation, and this was the first thing drawn that is not voxels.
**The 2026-08-11 scope change brought it inside**, so that note is history rather than
a caveat now.

It reuses the engine's shape of answer rather than its data: no vertex buffer, quads in
an SSBO, six corners from `gl_VertexID`, one draw call. A chunk `Quad` could not be
reused — six-bit lattice integers cannot describe a 0.5-block-wide limb that swings — so
character.vert carries explicit float edges instead. character.frag shares chunk.frag's
face-brightness table exactly, which is what keeps the model in the same light as the
ground rather than reading as a sticker on it.

**The third-person camera used to clip through terrain**, and the note here said fixing
it needed a voxel raycast the engine did not have. Phase 9 built one for aiming, and
the fix was four lines in `updateRenderCamera`: cast from the eye along the
displacement, stop a quarter block short of what it hits. That is the second caller of
`world/Raycast`, and it is the concrete reason Phase 9 was ordered ahead of vegetation.

**The camera goes over the right shoulder, not straight back**, and that came out of
play too. Straight back put it on the view axis -- which is where the character is
standing, so the model sat exactly on the crosshair and hid the block being aimed at.
Two consequences to know before touching it: the collision cast runs along the *whole*
displacement, because a lateral offset can push the camera through a wall just as a
backward one can; and the crosshair no longer sits at the centre of the screen, because
the ray is cast from the eye while the frame is drawn from the shoulder. `Engine::aimNdc`
is where those two points are reconciled, and in first person it returns the centre.

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
  command buffer has the identical problem, so fix both together. **Water added a second
  write to the same buffer** (the translucent pass's origins, at
  `m_origins.size() * sizeof(vec4)`), so there are two instances of the hazard now
  rather than one; a ring has to cover both.
- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- **World persistence** — **now in scope** (DESIGN.md Phase 11), so the open part is
  the disk format rather than the question. Sections are palette-compressed already,
  so what is undecided is the container and whether it compresses at all.
- **Aquifers, and with them flooded caves and lava lakes.** RESEARCH.md 5.3's third
  problem, and the only one water did not solve. Its internals are unpublished
  (RESEARCH.md 6), so this needs a source beyond the wiki. Until then caves under the
  sea are dry and a cliff overhang has a dry pocket at its foot — both documented at
  the code in `Generator`.
- **Flowing water.** The 20 Hz tick exists and `BlockUpdates::examine` is where it
  goes, but the safe-read argument there does not survive a sideways spread. See the
  note at that read.
- **The translucent pass does not sort back to front.** Correct blending of
  overlapping translucent surfaces needs it. Water gets away without it by being the
  only translucent thing and very nearly flat; a second translucent block type is
  where that stops being true.
- **Trees leave a two-block band along every column edge with no trees in it.** The
  deliberate cost of trees not crossing columns; see `TreeSpec` and DESIGN.md 7.9.
  Fixing it properly means a chunk-status pipeline like vanilla's.
- **Placing a block is refused only where the player stands, and the test is crude.**
  A two-block column at the feet with no width, matching the walk code's own shape.
  Since walking has no collision volume either, the two are at least consistent — but
  a real capsule would refuse cases this lets through, and building a proper collider
  should fix both together rather than one of them.
- **Items are `BlockId`s, and crafting will break that.** A stick is not a block. The
  split is deferred on purpose -- see DESIGN.md 7.10. It is a smaller job than it was:
  it now means changing what `ItemStack::block` is, rather than that plus building the
  container around it.
- **The UI layer handles exactly one window.** No widget tree, no event routing --
  `HudRenderer` plus `InventoryLayout` and nothing else. A chest or a crafting bench is
  a second window and is the point at which that stops being enough; its header says so.
- **Death drops nothing and shows nothing.** Respawn is full health where you stand.
  A death screen needs the second window above; dropping the inventory needs somewhere
  the player can get it back from.
- **A sand collapse pays the sky-light recompute twice per block.** `World::setBlock`
  relights the whole column, about 0.5 ms, and a falling block edits the world once
  leaving and once landing. Documented as a per-*click* cost when it was one; in open
  desert a six-block collapse is a dozen recomputes over as many ticks. Not measured
  in play yet. The incremental relight World.hpp already names is the escape hatch.
- **The character occludes anything within arm's reach in third person.** The
  shoulder offset separates them at normal range -- at a four-block target the
  character sits 20.6 degrees off centre and the aim point 10.3 -- but a block right
  at the player's feet cannot be seen past the model whatever the camera does. The
  block name under the crosshair is what covers that case; fading the character when
  it covers the aim point would cover it properly.
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

---

## 9. Engine, or game — **answered**

Raised by the user on 2026-08-10 after playing the engine twice, and **settled on
2026-08-11: the scope widens to include interaction.** DESIGN.md and README.md were
rewritten to say so in one commit before any feature work started, so no game feature
was ever added to a document that denied it was in scope. DESIGN.md section 1 carries
the scope statement; this is the part worth remembering about how it got there.

**How it came up.** The user asked for "all Minecraft objects" to be researched. That
research was deliberately scoped to the block types the *terrain generator* places,
because DESIGN.md ended the project at terrain generation, and it said so. Twenty-one
block types later the user played and reported seeing nothing new -- and both things
were true at once: the work was real, and **every one of those block types is
underground**, unreachable by a player who cannot dig. Then came questions about
building, crafting and hunting, none of which existed or was in scope.

**The research was not wrong; the scope was narrower than the question.** That is the
lesson, and it generalises past this project: a correct answer to the wrong question
still leaves someone with a world they cannot do anything in.

Two constraints the user attached to the decision, both still binding:

- **Libraries stay minimal.** Everything is written here. This has not constrained
  anything yet: `BlockTextures` generates every texture in code, `CharacterRenderer`
  added a whole non-voxel render path, and the UI layer and its 5x7 font were built the
  same way -- all with zero new dependencies. The one genuinely painful thing to
  self-implement would be audio, which is not on the list.
- **Time is accepted.** The user's framing was "it will take a long time, but" -- an
  argument against doing it all at once, not against doing it.

Everything that decision put on the roadmap is now built except vegetation beyond
trees, persistence, and crafting. See section 1.
