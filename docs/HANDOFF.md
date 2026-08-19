# Handoff

Snapshot for resuming work. Written 2026-08-09; last updated 2026-08-18, after the
seventh play session -- which found that **nobody could find crafting** -- and the
container-window layer and **crafting table** that came out of it.

Read `docs/DESIGN.md` for the full design, the reasoning behind every decision, and the
measured result of every phase; `docs/RESEARCH.md` for the vanilla Minecraft mechanics
and numbers those are measured against.

**This file is what those two are not: the practical detail needed to pick the work
back up cold, and nothing that is already finished.** It carries what to do next, how
to build and run it, where things live, and the mistakes that cost real time. It was
cut back to that on 2026-08-13, for the second time -- both times because it had filled
up with descriptions of completed work, which is the one kind of content a handoff has
no use for.

---

## 1. Where things stand

| Track | State |
|---|---|
| **Performance** — phases 0-8 | 0-3 done. **4 in progress**: 4a-4c built, **4d (biomes) is the last step and is *not* next** — it still has the unresolved input in section 6. 5-8 untouched. |
| **Interaction** — 9-15 | **Done**, plus trees, oceans, flowing water, held placement and a real player box. |
| **Crafting** — 16-19 | **16 and 17 done** -- the window layer, the crafting table, and the furnace. 18-19 open, below. |

The two tracks are independent, so 4d being open alongside finished interaction work
is not a contradiction.

| | Next up | Blocked on |
|---|---|---|
| **17** | Crafting bench, **furnace and smelting**, iron/diamond tiers, durability | a **second UI window** |
| **18** | Torches and block light | a torch is not a cube (Phase 10 geometry) |
| **19** | Mobs, combat, weapons, armour | **mobs do not exist at all** |

**17's real content is the furnace, not the bench.** Vanilla's harvest tiers are in the
block table already — iron, copper and lapis need a stone pickaxe; gold, redstone and
diamond need iron — so **half the ore table is deliberately out of reach today**, and
the wall Phase 16 stops against is precisely what 17 opens. A sword is craftable now
and does nothing, which is honest rather than finished.

**18's bit budget is already settled** (DESIGN.md 3.7): the 64-bit quad is exactly
full, so sky and block light combine into the brightness it already carries rather than
the word widening to 128 bits. The tint is what that costs.

**19 is last because armour has nothing to protect against.** Fall damage is the only
thing in the world that can hurt the player and there is nothing to fight.

> **What is built is written up in DESIGN.md 7.x, and this file does not repeat it.**
> RESEARCH.md carries the vanilla numbers it was measured against. Duplicating either
> here is how this document grew past a thousand lines twice; the second time it was
> mostly descriptions of work that was already finished, which is exactly the content
> a handoff does not need.

### The eighth session, and what it found

**2026-08-18, the held item.** The first thing anyone had ever seen of a tool in a
hand, and it took three rounds to get right. What play found, in order:

1. **The icons were placeholders and nobody had noticed**, because a slot is a
   forgiving place for a 16x16 sprite and a hand is not. Redrawn to fill the tile the
   way vanilla's do.
2. **The back face of the extruded sprite was drawing a reflection of its own rim**,
   which read as two objects crossing in an X. Found by drawing the faces and the rim
   in two separate captures.
3. **The character was gripping the pickaxe by its head.** Vanilla's third-person tilt,
   used as published, brings the top of the sprite to the fist -- and the top of the
   sprite is the metal. Half a turn in the sprite's plane fixed it.

Only the third of those was a wrong number. The other two were things that no test
could have failed on and no reasoning had reached, which is this project's oldest
lesson and the reason `--hold` now exists.

### Playing it is what has driven this project

**Every change of direction came out of a play session rather than out of reasoning
about scope.** That is the single most useful thing to know before picking this up.
Six sessions so far, written up in DESIGN.md 7.12, 7.14 and 7.15. Two are worth
carrying as method rather than as history:

- **Session 3 reversed a recommendation made in this file.** A count-based inventory
  was recommended and chosen on an argument that was *correct* — it closes the
  break-drop-collect-place loop for a fraction of the work — and the conclusion was
  wrong, because carrying things is not felt as a number going up. No amount of
  reasoning about scope was going to produce that, and one session did.
- **Item pickup had never worked, through four sessions and a full test suite**,
  because nothing printed a figure that would have been zero. Every counter in the
  stats line exists because of that, including the newest two.

### Where to resume

**Nothing is half-finished.** 321 tests, asan passes. **tsan is clean** over twelve
seconds of the running app, re-run on 2026-08-19 after the water phase.

Four of these need a person, and they are first on purpose — the list of things this
project found by playing is longer than the list it found by reasoning.

1. **Walk the whole chain to a diamond.** Tree, planks, table, sticks, wooden pickaxe,
   stone, stone pickaxe, iron ore, furnace, coal, ingot, iron pickaxe, diamond.
   **`tests/test_progression.cpp` now walks it end to end in the tables** -- recipes,
   tiers, drops and the smelt, through the real containers and the real click path --
   so what is left is the half a test cannot reach: aiming, mining, and finding the
   windows. That half is where both previous findings came from (nobody could find the
   grid; the fuel was off by one smelt), so this stays at the top of the list.
2. **Play the water, and this is now the top of the list rather than a formality.**
   Dig into the side of a lake above its bed and watch it pour in. **That case did
   nothing at all until 2026-08-19** (DESIGN.md 7.23), and the reason nobody knew is
   that a benchmark flight never edits the world so it never notifies a fluid. The
   stats line prints `flowed`, which is the number that should not be zero, and
   `(N suspended)` beside it.
   - **The per-flow cost has still never been measured.** Every flow is a `setBlock`
     and every `setBlock` relights a column, and blocks that used to return early now
     run a five-block slope search in four directions. Watch `updates queued`, and
     report a `Block update queue full` warning if one appears -- that is a cascade
     that does not terminate rather than a busy world.
   - **The surface now has a height, in four steps where vanilla has nine.** Whether
     the steps read as steps is the thing a capture cannot answer and this decides.
   - **Ten seconds in the real game would settle a deliberate deviation**: empty a
     bucket in mid-air and see whether vanilla makes a single column or a wide
     curtain. RESEARCH.md 7.1 has why it matters.
3. **Knock a sand pillar over.** Phase 12 has *still* never been seen by a person, for
   the same reason. Place a few sand blocks and dig out the bottom one. This is the
   oldest unverified thing in the project.
4. **Look at water from underneath, and press `F11` back to windowed.** Back-face
   culling is off for the translucent pass precisely so the surface reads from below,
   and `--capture` cannot put a camera under the sea. Both fullscreen sessions started
   from `--fullscreen`, so the path that restores the remembered windowed rectangle —
   the one with the Wayland caveat under it — has never run. Water is a short walk
   **west of spawn**.
5. **Light does not cross column borders.** A cave lit through an opening one column
   over stays dark, with a straight vertical boundary. Needs a light-changed signal
   threaded into the dirty-mask and pin machinery meshing already uses, so it is a
   phase rather than a patch. Section 6 has the shape of it.
6. ~~**The persistent-buffer hazard**~~ — **done 2026-08-18** (DESIGN.md 7.21). One
   `rhi::FrameRing`, owned by `Engine` and advanced once per frame; all five writes go
   through it and `SectionMeshStore` keeps its own discipline. Phase 5's command buffer
   is the sixth caller and needs no new machinery.

## 2. Commands

```bash
# Configure (only needed after CMakeLists changes; deps are cached in .cache/)
cmake --preset debug
cmake --preset release

# Build
cmake --build --preset debug
cmake --build --preset release

# Test  (321 cases, doctest)
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

# Compare frame times against a baseline commit. **This is the only sound way to do
# it** -- the numbers are not reproducible across sessions, so a table row from last
# week is not a baseline. Build the old commit in a throwaway worktree and interleave
# the runs, A/B/A/B, on an idle machine.
#
# **Both sides must go through the preset.** The first attempt at this configured the
# baseline with a plain -DCMAKE_BUILD_TYPE=Release, which skips the preset's LTO; the
# baseline meshed 0.6 s slower and the new code looked like an improvement it had
# nothing to do with. The tell was a number moving in a direction nobody had a
# mechanism for.
git worktree add /tmp/baseline <commit>
cd /tmp/baseline && CPM_SOURCE_CACHE=$OLDPWD/.cache cmake --preset release \
  && cmake --build --preset release
# ...run both binaries alternately, then:
git worktree remove --force /tmp/baseline

# Re-run the mesher comparison (off by default; it meshes a few hundred times)
./build/release/src/app/minecraft --mesh-benchmark

# What the terrain is actually made of. No GL, no window -- it generates columns and
# counts them. This is the only honest check on anything underground; see section 5.
./build/release/src/app/minecraft --probe --probe-columns 24

# Hold something, so a capture can show it. The held item is a model in the fist now
# -- an extruded sprite for a tool, the block itself for a block -- and crafting one
# by hand is the only other way to see it. Works in both views; `--first-person` is
# the one that shows the view model.
./build/release/src/app/minecraft --hold wooden_pickaxe --capture /tmp/shot.ppm
./build/release/src/app/minecraft --hold cobblestone --first-person --capture /tmp/shot.ppm

# Start flying rather than walking. No longer the only way underground -- digging
# works now -- but still the fastest way to go and look at something specific.
./build/release/src/app/minecraft --fly

# First person, if the character is in the way of what is being looked at.
./build/release/src/app/minecraft --first-person --capture /tmp/shot.ppm

# Start fullscreen. `F11` toggles it at runtime; this is for starting there, and it
# is also how a capture at the monitor's native resolution is taken.
./build/release/src/app/minecraft --fullscreen --capture /tmp/shot.ppm

# Open a **furnace's** window, part-way through a smelt, and capture. The gauges are
# the part most likely to be drawn wrong -- the flame's first placement was in the
# one-pixel gap between two slots and drew underneath them.
./build/release/src/app/minecraft --furnace --capture /tmp/shot.ppm

# Open a **crafting table's** window and seed it, then capture. A window is the only
# thing in the engine that needs a pointer to exist, so --capture cannot otherwise
# reach it. The table rather than the player's own 2x2, because the 3x3 is where a
# pickaxe is made and the smaller grid would show the smaller half of the feature.
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
| `Escape` | release cursor, then quit | same |
| **Left mouse** | **hold to break the highlighted block** | same |
| **Right mouse** | **place the held block against it** | same |
| **`1`-`9`** | **pick which hotbar slot to place from** | same |
| **`E`** | **open and close the inventory, and craft** | same |
| walk over an item | **pick it up** | same |

Third person is **over the right shoulder**, and the crosshair follows the aim ray
rather than sitting at the screen centre -- in third person those are not the same
point. The block under the crosshair is **named on the HUD**, which is the only thing
that works when the character is standing in front of what is being mined.

**Crafting is the `E` window.** Right click drags one item at a time into the 3x3
grid on its top right; the slot past the arrow shows what the grid would make, and
clicking it takes one and consumes one from every filled cell. Closing the window puts
the grid back in the pack rather than eating it. **A log in any cell makes planks**,
which is where every game of this starts.

Reach is 5 blocks. **Breaking is held, not clicked** -- cracks spread across the block
while the button is down, and the time is vanilla's hardness: 0.75 s for dirt, 2.25 s
for stone, 4.5 s for an ore, 6.75 s for one in deepslate. Letting go or looking away
abandons the progress, which is vanilla's rule and what stops a player chipping four
blocks at once by sweeping the crosshair. Placing stays a single click.

A left click with the cursor released re-captures it instead of breaking anything, so
`Escape` then click does not dig a hole.

**Bedrock cannot be broken** — it is the world's floor, and vanilla refuses for the
same reason.

**Stone breaks bare-handed and gives you nothing.** That is not a bug and it is
vanilla's rule: 7.5 seconds for an empty hand against 1.125 with a wooden pickaxe, and
no cobblestone at the end of the first one. It is the only thing that makes a pickaxe
worth the trip to a tree.

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
                 dedupe set, the retry discipline, **and the fluid flow** -- levels,
                 the five-block slope search, draining, and suspending at a column
                 that is not Ready
  Inventory    — the player's 36 slots and the stack in their hand. Nothing else:
                 the craft grid moved out in Phase 17
  ItemStack    — one slot's contents, in its own header because a grid, a furnace
                 and a chest all hold stacks and none should include the player
  Container    — **what a thing with slots is**: count, kind, access, take-output,
                 give-back. The interface a furnace and a chest plug into
  CraftingGrid — an N x N grid and its computed output. **The edge is the only
                 difference** between the player's 2x2 and a table's 3x3
  Screen       — one container plus the player's 36, in one flat index space, and
                 the click routing that used to live in Inventory. `releaseOne`
                 returns `{moved, spilled}` -- see the note in section 5
  PlayerBox    — the player's collision box, 0.6 wide. Height and eye height live
                 here rather than in the renderer, and section 5 says why
  ItemTable    — **every item, and the id space that extends BlockId's**; tools,
                 mining speed, harvest tiers, and what a block drops
  Crafting     — the recipe table and the 3x3 match (shaped, mirrored, shapeless)
  Tools        — ToolKind and ToolTier, in their own header because BlockTable and
                 ItemTable both need them and neither may include the other
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
  ScreenLayout (every slot rectangle of whatever window is open; the renderer AND
                the hit test use it, which is the whole reason it exists. Was
                InventoryLayout and knew one window; `ScreenKind` is what varies),
  HudRenderer (crosshair, hotbar, hearts, and whatever window is open — a small UI
               layer. A window is a list of slots, so the table cost it one loop)
src/app/
  main, Engine (streaming pipeline: submit-only frame loop, upload thread)

assets/shaders/         chunk.vert, chunk.frag, water.* (the translucent pass: it
                        reads bits 33..40 as a surface height, not as AO),
                        character.*, triangle.*
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
- **A back face samples its texture mirrored unless you say otherwise.** The texture
  coordinate comes from the quad's corner, so a face wound to point the other way puts
  column 0 at the far end -- the image is then a reflection of any geometry built from
  the same pixels. On an extruded sprite that means the drawn tool and its own rim
  cross in an X as soon as the back is visible. `ItemQuad::mirrorU` and one line in
  `character.vert` are the fix; the way to see it is to draw the faces and the rim in
  two separate captures.
- **An icon that reads in a slot can fail completely in a hand.** The tool sprites
  were placeholders -- a shaft eight pixels long in a sixteen-pixel tile -- which is
  fine at icon size and reads as a wooden cross once it is a model held near the
  camera. **And their per-pixel noise becomes a barcode on the rim**, because rim faces
  take the colour of the pixel they came from. Tool layers are drawn corner to corner
  and at a third of the roughness now. DESIGN.md 7.22.
- **Minecraft's model space is Y-down and Z-back; this engine's is Y-up and
  Z-forward.** Vanilla's published display transforms -- the numbers that place a held
  item -- are expressed in the first and land wrong in the second: the third-person
  tool goes *up* four sixteenths into the character's chest. Convert with a half turn
  about X applied to the transform (`C R C^-1`, `C T`) and **not** to the model, and
  never with a mirror: negating one axis reverses every winding and back-face culling
  then keeps exactly the wrong half. RESEARCH.md 9.3.
- **`CharQuad`'s `origin.w` is a texture layer, and 0 is a valid one.** Anything
  pushing a character quad has to write `ItemQuad::kFlatColour` there, or it draws
  textured with layer zero -- stone -- and says nothing about it.
- **Anything written every frame goes through `rhi::FrameRing`, never into a buffer of
  its own.** A persistently mapped buffer written at offset 0 each frame is data the GPU
  may still be reading from the frame before; coherence orders writes, it does not wait
  for a draw. Five renderers had this and none of them showed it, because vsync leaves
  enough slack to hide it. `Buffer::createPersistent` outside the ring is now for arenas
  whose ranges outlive their frame -- there is exactly one, and it defers reuse by three
  frames instead.
- **`glBindBufferRange` has an alignment the driver chooses**, and it is 16 bytes on
  this machine and 256 on plenty of others. `Buffer::storageOffsetAlignment()` queries
  it; nothing may assume a value, because getting it wrong is a hard GL error rather
  than a slow path. It is also why a ring slot's tail can be unusable: the next aligned
  offset can land exactly on the end of the slot.
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
  spreads sideways and the argument does not survive it**, which is why
  `World::isReadyAt` exists: it answers the question `blockAt` cannot, because that
  collapses "air", "not loaded" and "still generating" into one value. Anything new
  that reads a horizontal neighbour to decide an edit has to use it and re-queue
  rather than decide without it.
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
- **`ItemId` and `BlockId` are the same underlying type, so the compiler cannot tell
  them apart.** That is the deliberate price of item ids extending block ids rather
  than replacing them -- adding a block stays one line and gives it an item for free --
  but it means passing one where the other belongs converts silently. The place it
  bites is placement: right-clicking with a pickaxe would place whatever block sits at
  that id. `itemIsBlock` and `blockOfItem` are the seam, and every conversion has to
  go through them.
- **An A/B benchmark where only one side went through the preset is not an A/B.** The
  first baseline for Phase 16 was configured with a plain `-DCMAKE_BUILD_TYPE=Release`
  while the `release` preset also sets `CMAKE_INTERPROCEDURAL_OPTIMIZATION`. Without
  LTO the baseline meshed 0.6 s slower, which would have been read as Phase 16
  *improving* warm-up. **The tell was a number moving in a direction nobody had a
  mechanism for**, and that is the check worth keeping: an unexplained improvement is
  as suspicious as an unexplained regression.
- **The frame-time table in section 1 is not comparable across sessions, and there is
  now evidence rather than a suspicion.** Rebuilding `cd80f8e` and running it today
  gives p99 4.77-5.45 where that same commit is recorded at 4.88-5.18. Comparing rows
  of that table at one-millisecond resolution is unsound. Compare against a baseline
  you built and ran in the same sitting, interleaved run-for-run.
- **`usedSlots()` is a count and not an index.** The Phase 16 inventory demo seeded the
  craft grid with `clickSlot(usedSlots() - 1)` meaning "the stack I just added" and
  picked up an unrelated slot, which produced a capture of a grid full of the wrong
  items and no output. Seeding order is now load-bearing: the grid is filled first,
  into an empty pack, where slot 0 is the only predictable index.
- **A `discard` in a fragment shader disables early-Z for that draw.** `hud.frag`
  needed one so item icons could have a transparent background, and the hotbar draws
  every frame. It is the only mechanism anyone has for the ~0.3 ms of p99 that Phase 16
  added in all four A/B pairs, and it is unsettled -- see DESIGN.md 7.16.
- **An edit dirties more than its own section, and the third reason is the subtle
  one.** The section holding the block; every section the block *touches*, because AO
  reads a 3x3x3 and a corner block reaches seven neighbours across column boundaries;
  and wherever the sky light moved, which dirties the same section in the eight
  surrounding columns too, because the mesher's padded light grid reaches into them.
  That last one is why `computeSkyLight` returns a changed-sections mask -- without it
  every click would remesh nine columns, and underground, where digging happens, the
  honest answer is that no light moved at all.
- **`World::setBlock` refusing with `Busy` is what makes editing safe without a lock,
  and the pin is the whole argument.** Every reader of a column pins it, so one
  `pinned()` test covers every reader there can be, and the caller retries next frame.
  Take that test out and the failure is a use-after-free rather than a torn read,
  because `Palette::set` can reallocate the index array. **Every writer must retry**:
  a player will click again, but a block update or a fluid spread that gives up leaves
  the world wrong for ever.
- **The third-person camera's collision cast runs along the *whole* displacement, and
  the crosshair is not at the centre of the screen.** The camera sits over the right
  shoulder, so a lateral offset can push it through a wall exactly as a backward one
  can; and the aim ray is cast from the eye while the frame is drawn from the shoulder,
  so the two disagree by design. `Engine::aimNdc` reconciles them and returns the
  centre in first person. Changing either without the other puts the crosshair on a
  spot the player is not aiming at, which is a worse lie than the problem it fixes.
- **Recomputing a cell's state from its neighbours and spreading into a new cell are
  two different operations, and merging them does not converge.** The first flowing
  water let an *air* block work out its own level from its neighbours, on the
  reasonable-sounding grounds that a block just broken beside a lake is air and air is
  where water goes. That path carries neither the down-first rule nor the slope search,
  so water filled every reachable cell in every direction and the queue grew without
  bound -- 176 pending and climbing, 74 edits a tick, no sign of stopping. Vanilla
  splits the two and so does this now. **The symptom of getting it wrong is a
  simulation that never settles, not one that settles wrong.**
- **A test that builds terrain through `setBlock` pays a full column relight per
  block.** 625 of them is six seconds in an optimised build and minutes at `-O0`; the
  first flowing-water tests timed the whole file out and looked like an infinite loop.
  Tests that want *terrain* rather than *edits* should write into sections directly,
  which is what the generator does. The relight is deliberate and correct on a
  per-click path -- it is the volume that is wrong, and **flowing water pays it too**,
  which is the unmeasured cost in section 8.
- **`timeout` plus a piped `printf` loses the diagnosis.** stdout to a pipe is
  block-buffered, so a probe killed by a timeout prints nothing at all and looks like a
  hang with no information. `setvbuf(stdout, nullptr, _IONBF, 0)` is the difference
  between "it hangs" and "it hangs at tick 25 with 176 pending".
- **"Not solid" is not the same question as "can be flowed into", and conflating them
  froze every lake in the world.** `isFluidReplaceable` is `!isSolidBlock`, so water
  counts as replaceable -- correctly, because a flow may overwrite a weaker one. But a
  block already full of water cannot accept more, so water resting on water is not
  falling. Asking the wrong one made the down-first branch return before the sideways
  spread, and **only the bottom layer of any body of water could move**. `acceptsFalling`
  is the right test. DESIGN.md 7.23.
- **A test that builds its own world tests the world it thought of.** All eight
  flowing-water tests put one layer of water on stone, so `below` was always solid and
  the branch above was never asked the question -- through the entire life of the
  feature. This is 7.14's lesson again with the gap in a different place: not a seam
  between two modules, but a *shape of world* nothing constructed. `pool()` exists so
  the next one starts from water on water.
- **A fluid fix that is wrong does not settle, it diverges.** Letting water on water
  spread sideways without asking whether it is a *source* made every block of a
  fifteen-block waterfall spread seven blocks in four directions on the way down. The
  test suite did not fail; it stopped terminating, and `timeout` was how it reported.
  A lake is a stack of sources and a waterfall is a stack of falling water; nothing in
  the column is a source, and that is what tells them apart.
- **The slope search and the down-first branch must NOT use the same predicate.**
  Making them consistent looks like a cleanup and breaks water finding a hole: vanilla
  counts a hole that has already filled with water as still a hole, because the first
  water down it fills it, and a flow that stopped preferring that direction the moment
  it succeeded would start spreading backwards instead. RESEARCH.md 7.1.
- **Bits 33..40 of a `Quad` are AO on an opaque face and four corner drops on a fluid
  one.** The 64-bit word has been exactly full since smooth lighting, so the fluid
  level got in by taking the field AO was wasting on water -- vanilla does not shade
  water with AO either. Zero means a full block, which is why `CulledMesher` needed no
  edit at all. **`aoAwareMerging` masks those eight bits out of the merge key**, which
  on a fluid quad would silently flatten every water surface in the world, so the
  fluid pass always keys on the whole word. DESIGN.md 7.23 and Quad.hpp.
- **A corner drop is a question about a vertex, not about a face.** "How far below the
  top of its block is the surface here", answered zero for any vertex not on top of
  its block. That is what makes one field serve all six faces with no per-face branch
  in the shader, and it is what makes a side face's upper edge land exactly on the
  surface the top face draws rather than a fraction of a block proud of it.
- **An empty return that means two different things is this project's recurring bug,
  and there are now three of it.** `blockAt` answers air for a column that is not
  loaded; `usedSlots()` is a count that was read as an index; and `Screen::releaseOne`
  returned the spilled stack, so "nothing left to give back" and "it went into storage
  cleanly" were both an empty stack -- and the loop that empties a crafting grid on
  close gave back the first cell and **deleted the rest**. All three are invisible at
  the call site, because the wrong answer is a perfectly ordinary value. When a
  function can succeed, do nothing, or fail, it needs somewhere to say which.
- **Placement and walking disagree about how wide the player is, deliberately.**
  `PlayerBox` is vanilla's 0.6-wide box and is what placement refuses against; walking
  is still a single point with a ground probe. Placement therefore refuses cases
  walking allows, which is the safe direction. Fixing it properly is a swept capsule
  for both, and doing one without the other is how they would drift apart silently.
- **A crafting table has no memory and a chest must.** `Container::releaseOne` is
  virtual for exactly this: the grid gives its cells back when the window closes and a
  chest gives nothing. Making it a rule `Screen` applies to every container would empty
  the first chest anyone opens.
- CMake needs `LANGUAGES C CXX`; GLFW and glad are C.
- Ninja is not installed; presets use Unix Makefiles.

---

## 6. Phase 4 — what is left of it

**Goal:** FastNoise2 terrain generation. **Exit criterion:** infinite terrain traversal.
**4a, 4b and 4c are done**; DESIGN.md 7.6 has what they cost and why. Only the three
things below are still live.

### Sky light does not cross column borders

**The seam is the thing to fix next.** Propagation is column-local, so a cave lit
through an opening one column over stays dark, with a straight vertical boundary. The
vertical fill is exact and depends on nothing but the column's own heightmap, so open
sky and the surface are unaffected; the error is confined to cave interiors within
about fifteen blocks of a border. Fixing it means propagating between columns once
neighbours are loaded, and then re-meshing what changed — which is a light-changed
signal into the same dirty-mask and pin machinery meshing already uses. Getting that
wrong corrupts meshes rather than merely dimming them, so it is a phase of its own.

### Before touching the ore and blob parameters

Parameters live in `worldgen/FeatureTable.hpp`, sourced from RESEARCH.md 3 and then
calibrated — see the note in section 5, which is the part worth reading first. Two
invariants a change must not break:

- **Blobs are seamless across columns with no ordering between them.** A vein near a
  border overlaps its neighbour, so generating a column replays the features of all nine
  columns around it and keeps only what lands inside. That works because placement is a
  pure function of (seed, column, feature, attempt) with no sequential RNG state. Keep
  it stateless or the replay stops being cheap and starts being wrong.
- The air-exposure test treats **outside the column as solid**, because the honest
  answer needs a neighbour that may not be generated yet, and asking for it would put an
  ordering dependency between columns that the streaming design exists to avoid. The
  error is confined to blobs touching a border and errs toward placing ore.

### 4d — biomes, and the input it is still missing

Biomes come from the climate fields `DensityGraph` already computes. Minecraft uses a
6-parameter space (temperature, humidity, continentalness, erosion, weirdness, depth);
this engine has three of them today. Emerald and badlands gold land with it, both being
biome-gated and deliberately absent until then — shipping them biome-blind would put
emerald in every hillside.

**4d has an unresolved input, and that is why it is not next.** The wiki publishes only `temperature` and `downfall`
per biome, not the 6-parameter intervals that actually place them, and not a
systematic surface/filler block table. RESEARCH.md 6 records the search that failed;
the game's own worldgen data is where those numbers will have to come from. Settle
that before planning 4d, or the phase starts on a guess.

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

- ~~**`ChunkRenderer`'s section-origin buffer is overwritten while the GPU may be
  reading it.**~~ **Fixed 2026-08-18** (DESIGN.md 7.21). It was five writes across four
  renderers, every one at offset 0, every frame, with a barrier and nothing else -- and
  the barrier orders writes, it does not wait for last frame's draw. They all go through
  one `rhi::FrameRing` now: three frames' worth of room, one slot per frame, bump
  allocated within the slot. `SectionMeshStore` keeps its own deferred-reuse discipline,
  because its ranges outlive the frame that wrote them.

  **What to know before touching it**: the ring is advanced once, at the top of
  `Engine::renderFrame`, and nothing may write into it before that call or between a
  write and the draw that reads it. Phase 5's indirect command buffer is the sixth
  caller and needs no new machinery -- reserve a slice, fill it, `bind` it.

- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- **World persistence** — **now in scope** (DESIGN.md Phase 11), so the open part is
  the disk format rather than the question. Sections are palette-compressed already,
  so what is undecided is the container and whether it compresses at all.
- **Aquifers, and with them flooded caves and lava lakes.** RESEARCH.md 5.3's third
  problem, and the only one water did not solve. **The algorithm is now written down**
  -- RESEARCH.md 7.2 has the floodedness thresholds, the 16x40x16 cells and the
  fluid-level formula, which section 6 had recorded as needing a decompilation and did
  not. Only the *barrier* noise is still undocumented anywhere found. Until this is
  built, caves under the sea are dry and a cliff overhang has a dry pocket at its
  foot — both documented at the code in `Generator`.
- ~~**Flowing water.**~~ **Built 2026-08-13** (DESIGN.md 7.17), **and it did not work
  until 2026-08-19** (DESIGN.md 7.23): water resting on water never reached the
  sideways spread, so nothing but the floor of a lake could move. What is left of it:
  there is no lava, though `fluidLevel` and `fluidSource` are per block type and lava
  is the same algorithm with a step of 2; **the surface slopes in four steps where
  vanilla has nine**, and widening it means reinterpreting `material` on fluid quads
  the way `ao` already is; and **the per-flow cost is still unmeasured**, because a
  benchmark flight never edits the world so it never notifies a fluid. A flow is many
  `setBlock` calls and each relights a column, so a player digging into an ocean is
  how that will be found -- and the fix above is what makes that finally possible.
  **One deviation from vanilla is deliberate and unconfirmed**: a source spreads
  sideways off water only when the water below it is also a source. RESEARCH.md 7.1
  has the experiment that would settle it.
- **Phase 5 inherits the shader layout unchanged.** Indirect draw plus GPU culling:
  per-section data is already an array indexed by `gl_DrawID`, which means the same
  thing under `glMultiDrawElementsIndirect`, so only the command buffer's producer
  changes. The persistent-buffer hazard above has to be fixed *with* it.
- **Water is not mass-conserving in vanilla, and building it as if it were is the
  trap.** A source block is never consumed by flowing out of it, so a hole dug in the
  sea bed floods forever and the sea does not drop. A conservative fluid needs global
  per-body state -- how much water, where the surface is now -- which is exactly what
  a chunk-streaming world cannot cheaply keep. RESEARCH.md 7.1.
- **The translucent pass does not sort back to front.** Correct blending of
  overlapping translucent surfaces needs it. Water gets away without it by being the
  only translucent thing and very nearly flat; a second translucent block type is
  where that stops being true.
- **Trees leave a two-block band along every column edge with no trees in it.** The
  deliberate cost of trees not crossing columns; see `TreeSpec` and DESIGN.md 7.9.
  Fixing it properly means a chunk-status pipeline like vanilla's.
- ~~**Placing a block is refused only where the player stands, and the test is
  crude.**~~ **Half fixed 2026-08-18** (DESIGN.md 7.18). Placement uses `PlayerBox`, a
  real 0.6-wide box, so a player standing on a block boundary no longer has blocks put
  through their shoulder. **Walking still has no collision volume**, so the two now
  disagree and placement refuses cases walking allows -- the safe direction, and the
  note in section 5 says why doing one without the other is the trap. A swept capsule
  for both is the honest finish.
- ~~**Items are `BlockId`s, and crafting will break that.**~~ **Resolved in Phase 16.**
  Item ids extend the block id space rather than replacing it, so a stick exists and
  coal ore drops coal. What is left of it is the type-safety note in section 5.
- ~~**The UI layer handles exactly one window.**~~ **Built 2026-08-18** (DESIGN.md
  7.19). `Container`, `Screen` and `ScreenLayout` are the layer; a furnace or a chest is
  a `Container` subclass and one `ScreenKind` entry, and neither needs a line in
  `HudRenderer`. What is left of it: there is still no widget tree and no event routing,
  which is fine while every window is a list of slots and stops being fine at the first
  one that is not -- a death screen with a button on it.
- **No durability, so a tool never wears out.** The field belongs on `ItemStack` and
  means nothing until there are tiers worth wearing out. Phase 17.
- **A dropped tool is still a cube with a tool painted on it -- and it no longer has
  to be.** `render/ItemModel.cpp` builds the extruded sprite a *held* tool is drawn
  from (DESIGN.md 7.22), which is exactly the model a dropped one wants; what is
  missing is only that `ItemRenderer` has not been moved onto it. It draws one cube
  per entity from `gl_VertexID` with no per-entity geometry, so this is a real change
  of shape for that renderer rather than a call swap -- but the model is written.
- **A sword is craftable and completely inert.** It multiplies no mining speed and
  harvests nothing, because until Phase 19 there is nothing to swing it at. It exists
  so the recipe can.
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
