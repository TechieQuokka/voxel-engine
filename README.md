# voxel-engine

A voxel engine written from scratch in C++20 and OpenGL 4.6, targeting a Minecraft-like
world at an extreme render distance. No game engine, no rendering framework — the
renderer, chunk system, mesher, job system and terrain pipeline are all in this
repository.

Linux only, GCC only. That is a deliberate constraint, not a limitation to work around:
it removes a portability layer that would otherwise shape every decision below.

## What it does today

Streaming terrain generated from a FastNoise2 density-function graph, meshed with
binary greedy meshing, drawn with **one** `glMultiDrawArrays` call per frame — with
caves, ores, sky light, trees, oceans, and a world you can mine, collect, craft in and
build with.

| | |
|---|---|
| Render distance 16 | p99 frame time **0.85 ms** (no caves) / **6.0 ms** (with caves) |
| Render distance 24 | p99 **2.14 ms** (no caves) |
| Draw calls per frame | 1 |
| Section storage | palette-compressed, 4 bits/voxel typical, uniform sections free |
| Threads | 6 workers for generation and meshing, 1 upload thread, main thread never blocks |

Measured on an RTX 3060, release + LTO, flying at 40 blocks/s so streaming is included.

## Build

Requires GCC 13+, CMake 3.25+, and `python3` with `jinja2` (glad generates its loader at
configure time). Every other dependency is fetched and pinned by CPM.

```bash
cmake --preset release
cmake --build --preset release
./build/release/src/app/minecraft
```

You start walking, in third person. `WASD` to move, `Space` to jump, `LeftControl` to
sprint, `LeftShift` to sneak, mouse to look. A full block has to be jumped, not walked
up — vanilla's 0.6-block step height.

**Hold left click to break the highlighted block, hold right click to place them, `1`-`9`
to pick a hotbar slot.** Placing repeats four times a second while the button is held,
which is vanilla's rate and the difference between building a wall and clicking a
hundred times. Reach is 5 blocks, cracks spread across the block while you
mine it, and the time is vanilla's hardness — 0.75 s for dirt, 1.125 s for stone with
a wooden pickaxe. Bedrock is unbreakable.

**Stone breaks bare-handed in 7.5 seconds and gives you nothing.** That is vanilla's
rule, and it is the only thing that makes a pickaxe worth making — a tool that merely
saved time would be an optimisation.

Broken blocks drop as items, and walking over one collects it into the hotbar, which
shows what you are carrying. **`E` opens the inventory** — 36 slots with
vanilla's stack limit of 64, click to pick a stack up and put it down, right click to
split it in half or place one at a time.

**The same window crafts, and it is 2x2 — exactly as much as vanilla gives you.** Right
click items into the small grid on its top right and the slot past the arrow shows what
they make. A log makes four planks, two planks make sticks, and **four planks make a
crafting table**.

**Place the table and right click it.** That is the 3x3, and it is the only way to a
pickaxe: three planks across the top and two sticks down the middle does not fit in four
cells. Planks or cobblestone plus sticks make a pickaxe, axe, shovel or sword.

**Eight cobblestone in a ring makes a furnace.** Place it, right click it, ore on top
and coal underneath. Ten seconds a smelt, eight smelts to a coal, and the arrow fills
and the flame burns down while you watch. Iron ore becomes an ingot, an ingot makes an
iron pickaxe, and an iron pickaxe is the only thing that will get a diamond out of the
ground. That chain is vanilla's and every step of it is here.

Sneak while right clicking to build on top of the table rather than opening it.

**What you are holding is in your hand, not just on the HUD.** A tool is its icon
extruded a pixel thick -- vanilla's own trick -- held in the fist and swinging with
the arm; a block is held as a block. Both show in third person and as a view model in
first.

The block under the crosshair is **named on the HUD**, and the selection outline is
thick enough to pick out of a textured world. Third person is over the shoulder, so
the character is not standing on what you are aiming at.

`F11` goes fullscreen at your monitor's native resolution, and back.

Ten hearts, and falling more than three blocks costs you some. Nothing else damages
you yet.

Sand and gravel fall. Dig out from under a stack and it comes down one block at a
time, on a 20 Hz tick rather than at your frame rate.

There are oceans. You swim in them rather than standing on them, and falling into one
cancels the fall damage. **Water flows**: break into the side of a lake and it pours in,
down first and then sideways, seven blocks before it runs out. Down is free and sideways
is metered, which is what makes it run downhill rather than spread as a disc — and it
looks five blocks ahead for somewhere to fall, so it finds the edge of a cliff on
purpose instead of by accident.

**The surface has a height, and it slopes.** A flow sits lower than the source feeding
it, the walls of a stream come down to meet their own top, and the water moves — still
water drifts, and a flow scrolls the way it is running. No flooded caves yet, though:
those need vanilla's aquifer system, whose barrier noise is not published anywhere usable.

`F` switches to flying, `F5` to first person, `Escape` releases the cursor and again
quits.

Dig down. The caves, ores and darkness are the point, and they are not visible from the
surface.

```bash
ctest --preset debug                       # 322 test cases
cmake --preset asan && ctest --preset asan  # address + undefined
./build/release/src/app/minecraft --render-distance 16 --bench-seconds 20
./build/release/src/app/minecraft --capture /tmp/shot.ppm
./build/release/src/app/minecraft --furnace --capture /tmp/shot.ppm
./build/release/src/app/minecraft --hold wooden_pickaxe --first-person --capture /tmp/shot.ppm
```

## How it is built

| | |
|---|---|
| Sections | 32³, palette-compressed with 1/2/4/8-bit indices |
| World height | 384 blocks, Y from -64 to 320, 12 sections per column |
| Meshing | binary greedy — bitwise face culling over 32-bit occupancy columns |
| Geometry | **no vertex buffers**; 64-bit quads in an SSBO, expanded from `gl_VertexID` |
| Per-draw data | none — sections find their origin via `gl_DrawID` |
| Per-frame data | one shared ring, three frames deep; no renderer owns a buffer |
| Depth | reversed-Z with an infinite far plane, so the frustum has five planes |
| Terrain | density functions on a 4×8×4 interpolation grid, then surface rules, then carvers |
| Errors | exceptions only at init and load boundaries; `Result<T, E>` everywhere else |

Dependency direction is enforced at link time rather than documented: `mc_render` does
not link `mc_worldgen`, and glad, GLFW and FastNoise2 are all linked `PRIVATE` so their
types cannot appear in any public header.

## Documentation

- **[docs/DESIGN.md](docs/DESIGN.md)** — every architectural decision and the reasoning
  behind it, plus the measured result of each phase. This is the substantial document.
- **[docs/HANDOFF.md](docs/HANDOFF.md)** — how to pick the work back up cold: commands,
  repository map, and the mistakes that cost real time.

The design document keeps its wrong turns rather than quietly editing them, because the
corrections are usually more useful than the conclusions. Three examples:

- Section 4.1 argued that noise throughput was the bottleneck and sized a budget for
  1.26 billion per-voxel density evaluations. Minecraft evaluates its density function on
  a coarse grid and interpolates — 3,969 samples per column instead of 393,216. The
  library choice survived; the reasoning did not.
- Greedy meshing was implemented twice. The first version produced correct output and ran
  *slower* than the naive mesher, because its merge step still walked all 196,608 plane
  cells to find the ~4,800 holding a face. Building the bitmask is not the optimization;
  never touching the empty cells is.
- The frame-time benchmark was wrong twice over — the camera flew along its view
  direction and sank out of the bottom of the world, and it advanced by a fixed timestep
  while rendering at 5,000 FPS, so streaming could never keep up and the visible set
  emptied. It now reports how far it actually flew and how much work is outstanding,
  because a benchmark needs its own sanity check.

## Scope

Terrain generation and rendering, plus the interaction needed to play in the world that
gets generated. No multiplayer and no redstone.

The scope was widened on 2026-08-11. It previously ended at terrain generation, and the
reason it moved is worth stating plainly: the last two phases added caves, deepslate and
seven ores — twenty-one block types, **all of them underground** — and two recorded play
sessions reached none of it, because walking cannot get below the surface and there is no
way to dig. Generating a world nobody can enter is a strange place to stop. DESIGN.md
section 1 has the full reasoning.

Phases 0 through 3 are complete and Phase 4 (terrain generation) is in progress. Two
tracks remain, independent of each other:

- **Performance** — indirect draw with GPU culling, four-level LOD, brickmap ray marching
  for the far field, occlusion culling. This is what the render-distance target needs.
- **Interaction** — block placement and breaking, trees, item drops, a slot inventory
  with its window, a HUD, health, block updates, oceans and flowing water are all
  **done**. Still open: aquifers, the rest of vegetation, and persistence.
- **Crafting** — Phases 16 to 19. **16 and 17 are built**: items stopped being block
  types, there is a container-window layer, a crafting table gates the 3x3 recipes, and
  a furnace smelts — so iron and diamond are reachable and the whole ore table means
  something. Still open: durability, torches and block light, then mobs and combat.

Entities, a slot inventory with a UI layer under it, a game tick and a translucent
draw pass all exist now.

**The redesign the crafting track waited on is done.** Items were `BlockId`s, and a
stick is not a block; item ids now extend the block id space rather than replacing it,
so adding a block is still one line and gives it an item for free. **A pickaxe is
necessary rather than merely faster** — bare-handed stone takes 7.5 seconds and yields
nothing, which is vanilla's rule and the only thing that makes a tool worth making.

Iron and everything past it is deliberately out of reach: vanilla's tiers need a stone
pickaxe for iron ore and an iron one for diamond, and an iron pickaxe needs smelting.
Torches wait on somewhere to put block light — the 64-bit quad is exactly full, so sky
and block light combine rather than the word widening (DESIGN.md 3.7). **Mobs and
combat are last and are the largest**: armour would be a stat that never fires, because
fall damage is currently the only thing in the world that can hurt the player.

## Dependencies

Six, and every one of them is a foundation rather than engine work: GLFW (windowing),
glad (GL loader), glm (vector maths), FastNoise2 (SIMD noise), plus Tracy and doctest for
profiling and tests. There is no game engine, no scene graph, no meshing library and no
image loader — block textures are generated in code. The chunk storage, mesher, culling,
job system, allocator and lighting are all written here.

## Licence

MIT — see [LICENSE](LICENSE).
