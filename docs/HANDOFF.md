# Handoff

Snapshot for resuming work. Written 2026-08-09; updated 2026-08-10 at the end of
Phase 3.

Read `docs/DESIGN.md` for the full design and the reasoning behind every
decision. This file is the short version plus the practical details needed to
pick the work back up cold.

---

## 1. Where things stand

**Phases 0 through 3 are complete.** Next up is Phase 4. Phase 3's sub-steps and all
its measurements are in DESIGN.md 7.5.

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

Working tree is clean. **The repository is local only — never push, never create
a remote.**

What runs today: streaming terrain that holds a **p99 frame time of 2.08 ms at render
distance 16** — eight times under a 60 FPS budget — and 1.76 ms at distance 24.
Generation and meshing run on a 6-worker pool, uploads on their own thread, and the
whole visible set is drawn with **one** `glMultiDrawArrays`. The main thread only ever
submits; it never waits for a worker.

---

## 2. Commands

```bash
# Configure (only needed after CMakeLists changes; deps are cached in .cache/)
cmake --preset debug
cmake --preset release

# Build
cmake --build --preset debug
cmake --build --preset release

# Test  (132 cases, doctest)
ctest --preset debug

# Sanitizers. tsan is mandatory after touching MpmcQueue, JobSystem, or anything
# on the streaming path. See the ASLR note below for why setarch is needed.
ctest --preset asan
setarch $(uname -m) -R ./build/tsan/tests/mc_tests

# tsan over the whole running pipeline, including load/unload while jobs hold pins
TSAN_OPTIONS="suppressions=$PWD/tsan.supp report_mutex_bugs=0" \
  setarch $(uname -m) -R ./build/tsan/src/app/minecraft \
    --render-distance 6 --bench-frames 400

# Run
./build/debug/src/app/minecraft

# Render one frame headlessly and exit (warms the whole region up first)
./build/release/src/app/minecraft --capture /tmp/shot.ppm
convert /tmp/shot.ppm /tmp/shot.png     # ImageMagick is installed

# Frame-time distribution: vsync off, no cursor capture, camera flies forward.
# The only way to measure the exit criterion -- vsync makes every frame read 16.7 ms.
./build/release/src/app/minecraft --render-distance 16 --bench-frames 900

# Re-run the mesher comparison (off by default; it meshes a few hundred times)
./build/release/src/app/minecraft --mesh-benchmark
```

**Always measure on the `release` preset.** Debug is `-O0`; timings from it are
meaningless.

Controls: `WASD` move, `Space`/`LeftShift` up/down, `LeftControl` sprint, mouse
look, `Escape` releases the cursor and then quits.

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
5. **Git is local only.** Commit when asked. Never push, never `gh repo create`.
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
  Coords (+ Face enum, ChunkPosHash), BlockRegistry, Palette, Section,
  Chunk (12-section column), Neighbourhood (3x3x3 view), World (chunk map)
src/worldgen/           knows world, nothing above it
  Generator — placeholder heightmap; FastNoise2 replaces its body in Phase 4
src/mesh/               both meshers take a SectionNeighbourhood
  Quad (64-bit packed), ChunkMesh, CulledMesher, BinaryGreedyMesher
src/render/
  Camera, Frustum (5 planes -- no far plane), BlockTextures,
  SectionMeshStore (one persistently mapped arena), ChunkRenderer (one multi-draw)
src/app/
  main, Engine (streaming pipeline: submit-only frame loop, upload thread)

assets/shaders/         chunk.vert, chunk.frag, triangle.*
tests/                  doctest; links module libraries individually
tsan.supp               third-party race suppressions, with usage in its header
```

One static library per module. **Dependency direction is enforced at link
time**, not just documented: `mc_render` does not link `mc_worldgen`, and glad /
GLFW are linked `PRIVATE` so their types cannot appear in public headers.

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
- CMake needs `LANGUAGES C CXX`; GLFW and glad are C.
- Ninja is not installed; presets use Unix Makefiles.

---

## 6. Next: Phase 4

**Goal:** FastNoise2 terrain generation.
**Exit criterion:** infinite terrain traversal.

The shape of the work is already in place: `worldgen/Generator` has the right
interface — one call fills one column, reads nothing but its own parameters, writes
nothing but that column — and only its body changes. Add FastNoise2 as a **PRIVATE**
dependency of `mc_worldgen` so its types cannot escape into any public header, and
build the density-function graph from DESIGN.md 3.12.

Three things to carry in from Phase 3:

1. **Re-measure the AO merge ratio.** The 13.5-point figure in 7.3 comes from smooth
   heightmap terrain; caves and overhangs will make AO vary far more per face, which is
   exactly the case that could make AO-aware merging a bad trade after all.
   `--mesh-benchmark` still runs the comparison.
2. **Watch how generation scales across workers.** The placeholder manages 4x on 6
   threads because its inner loop is `Palette::set` — a palette scan plus a
   read-modify-write per voxel — so it saturates memory bandwidth before cores. A
   density-function generator is compute-bound and should do better. If it does not,
   writing through the palette one voxel at a time is the thing to fix.
3. **Caves break the "fully enclosed" saving.** Two fifths of all meshed sections
   currently produce zero quads because they sit inside solid rock. Carving caves
   through them turns each into real geometry, so both the arena sizing
   (`meshArenaBytesFor`, ~11 KiB/column measured) and the mesh timings need rechecking.

Phase 5 is indirect draw plus GPU culling. Note that the shader side is already
arranged for it: per-section data is an array indexed by `gl_DrawID`, which means the
same thing under `glMultiDrawElementsIndirect`, so only the command buffer's producer
changes.

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
| Errors | exceptions only at init/load boundaries; `Result<T,E>` everywhere else |
| Namespace | flat `mc`, except `mc::rhi` |

## 8. Still open

- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- **World persistence** — disk format, and whether it is in scope at all.
- **Re-measure AO merging in Phase 4**, once terrain has caves and overhangs.
- **`.clang-format` and `.clang-tidy` are listed in DESIGN.md 5.2 but do not
  exist.** Adding a formatter now would reformat the whole tree in one commit, so
  it is a deliberate decision rather than a chore — either add them and take that
  commit, or drop them from the document.
