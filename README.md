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
caves, ores, sky light, trees, and a world you can mine, collect and build with.

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

**Hold left click to break the highlighted block, right click to place one, `1`-`9` to
pick a hotbar slot.** Reach is 5 blocks, cracks spread across the block while you
mine it, and the time is vanilla's hardness — 0.75 s for dirt, 2.25 s for stone,
6.75 s for an ore in deepslate. Bedrock is unbreakable.

Broken blocks drop as items. Walk over one to pick it up; the hotbar shows what you
are carrying and placing spends it. **`E` opens the inventory** — 36 slots with
vanilla's stack limit of 64, click to pick a stack up and put it down, right click to
split it in half or place one at a time.

Ten hearts, and falling more than three blocks costs you some. Nothing else damages
you yet.

Sand and gravel fall. Dig out from under a stack and it comes down one block at a
time, on a 20 Hz tick rather than at your frame rate.

`F` switches to flying, `F5` to first person, `Escape` releases the cursor and again
quits.

Dig down. The caves, ores and darkness are the point, and they are not visible from the
surface.

```bash
ctest --preset debug                       # 213 test cases
cmake --preset asan && ctest --preset asan  # address + undefined
./build/release/src/app/minecraft --render-distance 16 --bench-seconds 20
./build/release/src/app/minecraft --capture /tmp/shot.ppm
```

## How it is built

| | |
|---|---|
| Sections | 32³, palette-compressed with 1/2/4/8-bit indices |
| World height | 384 blocks, Y from -64 to 320, 12 sections per column |
| Meshing | binary greedy — bitwise face culling over 32-bit occupancy columns |
| Geometry | **no vertex buffers**; 64-bit quads in an SSBO, expanded from `gl_VertexID` |
| Per-draw data | none — sections find their origin via `gl_DrawID` |
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
  with its window, a HUD, health and block updates are all **done**. Still open: water,
  the rest of vegetation, and persistence.

Entities, a slot inventory with a UI layer under it, and a game tick all exist now.
Water is the next thing wanted and is three changes at once: aquifers, a mesher that
can cull water against water, and a translucent draw pass. Mobs and combat remain
unplanned. Crafting is closer than it was — the window, cursor, hit testing and stack
limits it needs are built — but it still forces items to stop being block types, which
is a redesign rather than an addition.

## Dependencies

Six, and every one of them is a foundation rather than engine work: GLFW (windowing),
glad (GL loader), glm (vector maths), FastNoise2 (SIMD noise), plus Tracy and doctest for
profiling and tests. There is no game engine, no scene graph, no meshing library and no
image loader — block textures are generated in code. The chunk storage, mesher, culling,
job system, allocator and lighting are all written here.

## Licence

MIT — see [LICENSE](LICENSE).
