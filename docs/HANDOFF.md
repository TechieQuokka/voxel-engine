# Handoff

Snapshot for resuming work. Written 2026-08-09, end of Phase 2. Updated
2026-08-10 with the pre-Phase-3 cleanup pass.

Read `docs/DESIGN.md` for the full design and the reasoning behind every
decision. This file is the short version plus the practical details needed to
pick the work back up cold.

---

## 1. Where things stand

**Phases 0, 1 and 2 are complete. Phase 3 is in progress — 3a through 3e done, 3f
next.** The sub-step table and the measurements are in DESIGN.md 7.5.

| Commit | Contents |
|---|---|
| `9c60ddd` | Phase 0 — project skeleton, GL 4.6 context, triangle |
| `207fd7d` | Phase 1 — palette-compressed sections, culled meshing, camera |
| `8180945` | Phase 2 — binary greedy meshing, block texture array |
| `9945f21` | Cleanup pass before Phase 3 (DESIGN.md 7.4) |
| `0d73b2b` | Phase 3a — lock-free MPMC queue and worker pool |
| `1c84b5e` | Phase 3b — chunk columns, world streaming, placeholder generator |
| `7c29082` | Phase 3c — neighbour-aware boundary culling and AO |

Working tree is clean. **The repository is local only — never push, never create
a remote.**

What runs today: streaming terrain at render distance 16, drawn with **one**
`glMultiDrawArrays` per frame and hierarchical frustum culling. Median frame time is
0.35 ms — but p99 is 29.7 ms, because generation and meshing still run on the main
thread. 3f moves them onto the worker pool, which is the whole remaining gap to the
exit criterion. See DESIGN.md 7.5.

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

# Sanitizers. tsan is mandatory after touching MpmcQueue or JobSystem.
ctest --preset asan
setarch $(uname -m) -R ./build/tsan/tests/mc_tests   # see the ASLR note below

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
  main, Engine

assets/shaders/         chunk.vert, chunk.frag, triangle.*
tests/                  doctest; links module libraries individually
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
- CMake needs `LANGUAGES C CXX`; GLFW and glad are C.
- Ninja is not installed; presets use Unix Makefiles.

---

## 6. Phase 3 — in progress

**Goal:** job system, chunk streaming, frustum culling.
**Exit criterion:** render distance 16 with a stable frame time.

Sub-steps and measurements are tracked in DESIGN.md 7.5. **3a through 3e are
done.** What remains:

**3f — wire the job pool in.** Generation and meshing onto the worker pool, plus the
upload thread from DESIGN.md 3.13. Ends with a Tracy capture. Deliberately last:
getting streaming right single-threaded first means a later bug is either a streaming
bug or a race, not ambiguously both.

**The upload thread needs no GL context of its own**, contrary to what this file said
earlier. That was the wrong conclusion: the arena is persistently and coherently
mapped, so writing to it is a plain memcpy into process memory and issues no GL
command at all. Any thread may do it. The only GL requirement is one
`rhi::Buffer::barrierAfterClientWrites()` per frame on the context-owning thread —
coherence removes the need to flush, not the need to order.

The hazard 3f has to handle instead is lifetime: a meshing job borrows
`const Section*` from up to nine columns and holds them across frames, so the World
must not unload one underneath it. That is what `Chunk::pin()` is for, and
`updateLoadedRegion` already retains pinned columns.

Phase 4 is terrain generation with FastNoise2, replacing the body of
`worldgen/Generator`. Note that **the AO merge measurement should be repeated
then**: the 13.5-point figure comes from smooth heightmap terrain, and caves and
overhangs will make AO vary far more.

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
