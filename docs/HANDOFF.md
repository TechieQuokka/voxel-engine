# Handoff

Snapshot for resuming work. Written 2026-08-09, end of Phase 2.

Read `docs/DESIGN.md` for the full design and the reasoning behind every
decision. This file is the short version plus the practical details needed to
pick the work back up cold.

---

## 1. Where things stand

**Phases 0, 1 and 2 are complete.** Next up is Phase 3.

| Commit | Contents |
|---|---|
| `9c60ddd` | Phase 0 — project skeleton, GL 4.6 context, triangle |
| `207fd7d` | Phase 1 — palette-compressed sections, culled meshing, camera |
| `8180945` | Phase 2 — binary greedy meshing, block texture array |

Working tree is clean. **The repository is local only — never push, never create
a remote.**

What runs today: a single 32³ section is generated, meshed with binary greedy
meshing, textured from an array texture with ambient occlusion, and drawn at
60 FPS with a free-flying camera.

---

## 2. Commands

```bash
# Configure (only needed after CMakeLists changes; deps are cached in .cache/)
cmake --preset debug
cmake --preset release

# Build
cmake --build --preset debug
cmake --build --preset release

# Test  (41+ cases, doctest)
ctest --preset debug

# Run
./build/debug/src/app/minecraft

# Render one frame headlessly and exit
./build/release/src/app/minecraft --capture /tmp/shot.ppm
convert /tmp/shot.ppm /tmp/shot.png     # ImageMagick is installed
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
CMakePresets.json       debug / asan / release / release-tracy
cmake/CompilerWarnings.cmake

src/core/               no dependencies
  Types, Math (only file including glm), Result<T,E>, BitPack,
  Log, Assert, Profile (Tracy macros), Paths
src/platform/           GLFW lives here and nowhere else
  Window, Input, Clock
src/rhi/                GL abstraction; no GL type in any header
  Device, Buffer, Shader, Texture, VertexArray
src/world/              pure data; knows nothing about rendering
  Coords (+ Face enum), BlockRegistry, Palette, Section
src/mesh/
  Quad (64-bit packed), ChunkMesh, CulledMesher, BinaryGreedyMesher
src/render/
  Camera, ChunkRenderer, BlockTextures
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
- CMake needs `LANGUAGES C CXX`; GLFW and glad are C.
- Ninja is not installed; presets use Unix Makefiles.

---

## 6. Next: Phase 3

**Goal:** job system, chunk streaming, frustum culling.
**Exit criterion:** render distance 16 with a stable frame time.

Expected work:

1. `core/JobSystem` and `core/MpmcQueue` — worker pool on `std::jthread`,
   lock-free queues. **The main thread must never block** on generation,
   meshing, or upload.
2. `world/Chunk` (a column of 12 sections) and `world/World` (the chunk map plus
   load/unload around the camera).
3. **Neighbour-aware boundary culling.** Both meshers currently emit boundary
   faces because a section is meshed in isolation. Once the World can supply
   the six neighbouring sections, `emitBoundaryFaces` gets replaced by real
   neighbour lookups — otherwise every chunk seam renders a redundant wall of
   quads.
4. `render/Frustum` and hierarchical frustum culling: chunk column first, then
   section.
5. Multi-chunk rendering. `ChunkRenderer` currently holds exactly one static
   buffer; it needs per-section buffers or a shared arena. This is also where
   `rhi::Buffer` should grow persistent mapping and triple buffering — the
   interface was shaped for it.

Phase 4 is terrain generation with FastNoise2. Note that **the AO merge
measurement should be repeated in Phase 4**: the 13.5-point figure comes from
smooth heightmap terrain, and caves and overhangs will make AO vary far more.

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
