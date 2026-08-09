# Voxel Engine — Design Document

> Status: **Draft / under discussion.** Nothing in this document is implemented yet.
> Last updated: 2026-08-09

A high-performance voxel engine written from scratch in C++20, targeting a
Minecraft-like world with an extreme render distance. Scope ends at terrain
generation — no redstone, mob AI, or multiplayer.

---

## 1. Fixed Constraints

| Constraint | Decision |
|---|---|
| Platform | Linux only. No cross-platform requirement. |
| Language | C++20 |
| Compiler | GCC |
| Engine | Custom renderer. No Unity/Unreal. |
| Scope | Up to terrain generation |
| Primary concern | Performance |

### 1.1 Consequences of Linux-only + GCC

- No MSVC compatibility layer, no `__declspec`, no DLL export macros.
- GCC builtins are freely available. `__builtin_ctzll` / `__builtin_clzll` are
  used directly by the binary greedy mesher — these are the core of the
  algorithm, not an optimization detail.
- OpenGL 4.6 is fully supported by both the NVIDIA proprietary driver and Mesa.

### 1.2 C++20 usage policy

Use: `concepts`, `ranges`, `std::span`, `std::bit_cast`, `std::countr_zero`,
designated initializers, `constinit`, `std::jthread`.

**Do not use C++ modules.** GCC's module implementation is still unstable and
its interaction with CMake is fragile. Header-based compilation only.

`std::format` is available and used for all logging, so no formatting library
(fmt, spdlog) is needed. `std::expected` is **not** available — see 6.2.

### 1.3 Verified development environment

Measured on the development machine during Phase 0:

| | |
|---|---|
| Compiler | GCC 13.3.0 |
| CMake | 3.28.3 |
| Generator | Unix Makefiles (Ninja is not installed) |
| GPU | NVIDIA GeForce RTX 3060 |
| Driver GL version | OpenGL 4.6 |
| Session | Wayland, with X11 available |

The RTX 3060 is the reference target for the render-distance-64 goal. Note that
the compositor blocks external screenshot tools, which is why the engine
captures frames itself (see 7.1).

---

## 2. Performance Target

**Render distance 64 chunks (1024 blocks radius) at 60 FPS.**

This is the single number that justifies every architectural decision below.
It is an aggressive target — comparable in scale to the Distant Horizons mod
rather than to vanilla Minecraft.

### 2.1 What the target implies

Computed for a 1024-block radius circle with a world height of 384:

| Metric | Value |
|---|---|
| Chunk columns within view | pi * 64^2 ~= 12,900 |
| Total voxels | ~1.26 billion |
| Uncompressed storage at 1 byte/voxel | ~1.2 GB |
| 32^3 chunk sections | ~38,600 |

Three conclusions follow, and they are not optional:

1. **Memory is the first wall.** A flat uncompressed array is impossible.
   Palette compression is mandatory, not an optimization.
2. **LOD is mandatory.** Keeping ~38,600 sections at full detail is impossible
   in both memory and generation time.
3. **The real bottleneck is likely terrain generation, not rendering.**
   Evaluating density noise for 12,900 columns costs more than meshing them.
   This drives the choice of noise library (see 4.1).

---

## 3. Architecture Decisions

### 3.1 Rendering approach — Hybrid

Rasterization for the near field, GPU ray marching for the far field.

**Build order matters:** the raster pipeline is completed first, and the ray
marching far field is layered on afterwards. This is sequencing, not a scope
reduction. The ray marching pipeline consumes the same voxel storage the raster
path produces, so the data layer must be stabilized first or it gets rewritten
twice. Distant Horizons was built in this order.

### 3.2 Graphics API — OpenGL 4.6, behind a thin RHI

OpenGL 4.6 provides everything the target needs: persistent mapped buffers,
bindless textures, `glMultiDrawElementsIndirect`, and compute shaders.

A Vulkan backend is added **only after profiling proves CPU submission is the
bottleneck.** Note that Mojang, with a 17-year-old OpenGL codebase and far more
resources, still ships Vulkan as an opt-in backend sitting *beside* OpenGL
rather than replacing it (snapshot 26.2, experimental). The same strategy
applies here.

Rendering code is written against a thin RHI abstraction so a second backend
remains possible without touching the chunk/meshing layers.

### 3.3 Chunk dimensions — 32^3

| Size | Section count | Re-mesh cost per block edit |
|---|---|---|
| 16^3 | ~154,000 | ~2-5 us |
| **32^3** | **~38,600** | **~10-20 us** |
| 62^3 | ~5,300 | ~74 us |

32^3 is the balance point:

- ~38,600 sections is a per-frame iteration count the CPU can absorb;
  16^3 would require managing over 150,000.
- 32 is half a 64-bit word, so bitwise meshing optimizations still apply.
- Divides exactly into 4^3 bricks of 8^3, which matches the ray marching
  brickmap structure (see 3.9).
- Gives four clean LOD levels: 32 -> 16 -> 8 -> 4.

62^3 is the native size for binary greedy meshing (a 64-bit word covers one
full axis plus 1-bit padding on each side) and produces the best merge ratios,
but a 74 us re-mesh on every block edit is unacceptable for interaction.

### 3.4 World height — 384 (Y from -64 to 320)

Twelve 32^3 sections vertically.

Increasing height is cheap because of the uniform-section optimization (3.5):
an all-air section allocates no voxel data at all, so the added sky costs a
few dozen bytes of header each. Real memory growth over a 256-height world is
roughly 10-15%, not 50%.

Height matters visually at this render distance. Terrain 1 km away needs
150-200 blocks of relief to read as mountainous. A 256 world with sea level at
64 leaves only ~190 blocks, which is tight once underground space is included.
This is the same reasoning behind Minecraft's 1.18 extension to -64..320.

**Unbounded vertical was rejected.** It replaces O(1) fixed-array section
lookup (`sections[12]`) with hash map probing, and that cost lands in a code
path executed tens of thousands of times per frame. The benefit does not
justify it for a terrain-generation-scoped project.

### 3.5 Chunk storage — palette compression

```
Section (32^3 = 32,768 voxels)
  palette      : vector<BlockId>       // only block types present in this section
  bitsPerIndex : 0 / 1 / 2 / 4 / 8     // derived from palette.size()
  indices      : vector<uint64_t>      // bit-packed indices
```

- `bitsPerIndex == 0` marks a **uniform section**: the index array is not
  allocated at all. Sky, bedrock, and deep ocean floor all fall into this case,
  which covers an estimated 60-70% of all sections.
- Terrain typically contains 4-16 distinct block types per section, so most
  populated sections land at **4 bits per voxel**.
- Expected footprint: ~1.2 GB uncompressed becomes roughly **150-180 MB**.

The palette must repack (grow or shrink `bitsPerIndex`) when block edits change
the palette size. Shrinking is deferred and batched — it is never on the hot
edit path.

### 3.6 Meshing — binary greedy meshing, with a caveat

Binary greedy meshing works in three steps: build a 64-bit occupancy mask, cull
64 faces at a time with bitwise operations, then merge the resulting face masks
into larger quads. Reported throughput is roughly 50-200 us per 62^3 chunk.

**The caveat: greedy meshing conflicts with ambient occlusion.**

Per-vertex AO values differ between adjacent faces, which breaks the merge
condition. Published measurements put the benefit at only ~15% when materials
are numerous and AO is enabled, versus 60%+ under favourable conditions. This
is why Minecraft itself does not use greedy meshing. Do not assume the
optimistic number.

Mitigations, all three applied together:

- **Use `GL_TEXTURE_2D_ARRAY` instead of a texture atlas.** No bleeding, no
  manual UV padding, and a per-quad layer index means same-material faces merge
  freely.
- **Near chunks: AO-aware merging.** Merge only when the AO pattern matches.
  Lower merge ratio, visual quality preserved.
- **Far / LOD chunks: unrestricted greedy merging.** AO is not perceptible at
  that distance, so merge maximally.

The actual gain from AO-aware merging must be **measured in Phase 2** before
committing to it.

### 3.7 Vertex format — no vertex buffer

Greedy meshing produces quads. Store the quads in an SSBO and expand corners
programmatically from `gl_VertexID` (programmatic vertex pulling). No vertex
buffer, no index buffer.

```
One quad = 64 bits
  pos.x(6) pos.y(6) pos.z(6)      // position within the 32^3 section
  width(6) height(6)              // merged extent
  normal(3) ao(8) texLayer(16)    // face direction, AO, texture layer
```

8 bytes per quad instead of four vertices. Lower bandwidth, and it composes
cleanly with indirect drawing.

### 3.8 Draw submission — GPU-driven

All visible chunks are drawn with a **single `glMultiDrawElementsIndirect`
call.** The indirect command buffer is filled by a compute shader that performs
frustum culling on the GPU. The CPU issues one or two draw calls per frame.

Buffers are persistent mapped and triple-buffered to avoid stalls.

### 3.9 Culling — three tiers

1. **Hierarchical frustum culling** — chunk column first, then section.
2. **Occlusion culling** — hierarchical Z-buffer built from the previous
   frame's depth, reprojected.
3. **Visibility graph** — Minecraft's approach. Precompute which faces of a
   chunk are mutually reachable through open space, then propagate visibility
   by BFS from the camera chunk. Highly effective underground and in caves.

Which of (2) and (3) to keep is decided by profiling in Phase 8. It may be both.

### 3.10 LOD — four levels plus ray marching

| Level | Resolution | Distance (chunks) |
|---|---|---|
| L0 | 32^3 full detail | 0 - 8 |
| L1 | 16^3 (2x downsample) | 8 - 20 |
| L2 | 8^3 (4x) | 20 - 40 |
| L3 | 4^3 (8x) | 40 - 64 |
| L4+ | brickmap ray marching | 64+ |

Downsampling uses a **mode (most-frequent) filter**, not averaging — averaging
block IDs is meaningless.

LOD boundary cracks are handled with **skirts**: a downward-extending wall along
each chunk edge that hides the seam. Not perfect, but near-zero cost and the
most widely used solution in practice.

### 3.11 Far-field ray marching (Phase 7)

Beyond L3, no mesh is built. The world is kept as a **brickmap** — a grid of
8^3 bricks uploaded to VRAM as a 3D texture — and rendered by DDA ray marching
in a fullscreen pass. A 32^3 chunk divides exactly into 4^3 bricks, so the
structures interlock.

The ray marching pass writes depth, so it composites with the near-field raster
result through the normal depth test.

### 3.12 Terrain generation

Implements Minecraft 1.18's density-function model on top of a FastNoise2 node
graph. A density value is computed per block position; `density > 0` is solid,
otherwise air.

```
continentalness -+
erosion          +--> terrain shaper --> base density
peaks & valleys -+                            |
                                    3D noise warp (overhangs)
                                              |
                                       density > 0 -> solid
                                              |
                                     surface rule -> block placement
```

The pairing is deliberate: Minecraft's density-function architecture *is* a
node graph, and FastNoise2 *is* a node-graph noise engine. The proven terrain
design maps onto the fast engine without translation.

### 3.13 Threading

```
Main thread     : input -> simulation -> draw submission
Generation pool : density evaluation -> section fill    (N-2 threads)
Meshing pool    : binary greedy meshing                 (shared)
Upload thread   : persistent mapped buffer writes       (1)
```

Connected by lock-free queues. Workers are `std::jthread`. **The main thread
never blocks** on generation, meshing, or upload.

---

## 4. Dependencies

Deliberately minimal. Everything with runtime cost is written in-house;
libraries are used only where they cost nothing at runtime or where a
hand-written version would be strictly worse.

| Purpose | Library | Rationale |
|---|---|---|
| Window / input / GL context | **GLFW 3.4** | Small C library doing exactly windowing, input, GL context, and Vulkan surfaces. SDL3 additionally drags in audio, threading, and filesystem abstraction — all out of scope here, and this is a Linux-only project so its portability layer is wasted weight. |
| GL function loader | **glad2** | Generates OpenGL 4.6 core plus only the extensions used. Two headers. |
| Math | **GLM 1.0.x** | Its SIMD support is experimental and flag-dependent, which normally counts against it — but all heavy vector math here runs on the GPU, and the CPU side is a handful of matrices for frustum culling. SIMD is not the bottleneck, so GLM remains the right choice. |
| Noise | **FastNoise2** | See 4.1. |
| Image loading | **stb_image** | Single header. PNG is sufficient. |
| Debug UI | **Dear ImGui** | Chunk statistics, profiler overlay, live noise parameter tuning. For a terrain-generation project this is a required tool, not a luxury. |
| Profiling | **Tracy** | Per-frame CPU/GPU timeline. Required to verify performance claims with numbers rather than impressions. |
| Build | **CMake**, deps via **CPM.cmake** | Ninja is preferred but is not installed on the dev machine, so presets use Unix Makefiles. Switching is a one-line preset change. |

Written in-house: renderer, chunk system, meshing, LOD, job system, terrain
generation pipeline.

### 4.1 Why FastNoise2 specifically

Section 2.1 concluded that terrain generation, not rendering, is the likely
bottleneck. FastNoise2 addresses that directly.

| Library | 3D Perlin throughput |
|---|---|
| libnoise | 0.65 M points/s |
| **FastNoise2 (AVX2)** | **261 M points/s** |

Roughly **400x**. 3D value noise reaches 494 M points/s. The reason is
architectural: the node graph keeps intermediate values in SIMD registers
through the whole pipeline instead of spilling to memory between stages.
SSE2 / SSE4.1 / AVX2 / AVX512 are selected by runtime dispatch.

Applied to this project's scale — ~1.26 billion voxels of 3D density — AVX2
evaluation is on the order of **5 seconds single-threaded**, under a second
across 8 cores. The same work with libnoise would take roughly half an hour.

---

## 5. Project Structure

### 5.1 Layering

Dependencies flow in one direction only. Circular dependencies are the first
thing that collapses structure in a project this size, so the layering is fixed
before the directories.

```
app        <- top level, may know everything
 ^
render     <- composes world / mesh / rhi
 ^
mesh  worldgen    <- know world, know nothing about render
 ^      ^
world      <- pure data. Knows nothing about rendering.
 ^
rhi        <- OpenGL abstraction. Knows nothing about voxels.
 ^
platform   <- GLFW wrapper
 ^
core       <- no dependencies
```

The critical invariant is that **`world` does not know `render`**. Chunk storage
feeds both the raster path and the Phase 7 ray marcher, so it must not depend on
either. Holding this line means adding the ray marcher later requires no change
to `world`.

### 5.2 Directory tree

```
minecraft/
├── CMakeLists.txt
├── CMakePresets.json          # debug / release / release-tracy
├── cmake/
│   ├── CPM.cmake
│   └── CompilerWarnings.cmake
├── .clang-format
├── .clang-tidy
├── .gitignore
│
├── docs/
│   └── DESIGN.md
│
├── assets/
│   ├── shaders/
│   │   ├── chunk.vert         # expands quads from gl_VertexID
│   │   ├── chunk.frag
│   │   ├── cull.comp          # GPU frustum culling -> indirect buffer
│   │   ├── hzb_build.comp     # Phase 8
│   │   └── raymarch.frag      # Phase 7
│   └── textures/
│       └── blocks/
│
├── src/
│   ├── core/
│   │   ├── Types.hpp          # BlockId, fixed-width aliases, GL loader types
│   │   ├── Math.hpp           # glm aliases (only file that includes glm)
│   │   ├── Result.hpp         # Result<T, E>; replaced by std::expected under C++23
│   │   ├── Paths.hpp/.cpp     # executable-relative asset resolution, file reads
│   │   ├── BitPack.hpp        # palette index packing/unpacking
│   │   ├── Log.hpp/.cpp
│   │   ├── Assert.hpp/.cpp
│   │   ├── Profile.hpp        # Tracy macros, no-op when disabled
│   │   ├── JobSystem.hpp/.cpp
│   │   └── MpmcQueue.hpp      # lock-free queue
│   │
│   ├── platform/
│   │   ├── Window.hpp/.cpp    # only file that knows GLFW
│   │   ├── Input.hpp/.cpp     # polled keyboard/mouse snapshot
│   │   └── Clock.hpp          # steady_clock wrapper
│   │
│   ├── rhi/
│   │   ├── Device.hpp/.cpp      # GL loading, debug callback, reversed-Z setup
│   │   ├── Buffer.hpp/.cpp      # immutable storage now, persistent mapped in Phase 3
│   │   ├── Shader.hpp/.cpp      # graphics + compute
│   │   ├── Texture.hpp/.cpp     # 2D_ARRAY, 3D
│   │   ├── VertexArray.hpp/.cpp # empty VAO required by core profile
│   │   └── IndirectDraw.hpp     # draw command structs
│   │
│   ├── world/
│   │   ├── Coords.hpp         # BlockPos / SectionPos / ChunkPos conversions
│   │   ├── BlockRegistry.hpp/.cpp
│   │   ├── Palette.hpp/.cpp   # palette + bit-packed indices
│   │   ├── Section.hpp        # 32^3, uniform optimization
│   │   ├── Chunk.hpp/.cpp     # column of 12 sections
│   │   └── World.hpp/.cpp     # chunk map, streaming
│   │
│   ├── worldgen/
│   │   ├── DensityGraph.hpp/.cpp   # FastNoise2 wrapper
│   │   ├── TerrainShaper.hpp/.cpp  # continentalness / erosion / peaks-valleys
│   │   ├── SurfaceRule.hpp/.cpp
│   │   └── Generator.hpp/.cpp      # worker entry point
│   │
│   ├── mesh/
│   │   ├── Quad.hpp                # 64-bit packed format
│   │   ├── ChunkMesh.hpp           # a flat vector of quads
│   │   ├── CulledMesher.hpp/.cpp   # reference mesher; oracle for Phase 2
│   │   ├── BinaryGreedyMesher.hpp/.cpp
│   │   └── Downsample.hpp/.cpp     # LOD mode filter
│   │
│   ├── render/
│   │   ├── Camera.hpp/.cpp
│   │   ├── Frustum.hpp
│   │   ├── BlockTextures.hpp/.cpp
│   │   ├── ChunkRenderer.hpp/.cpp
│   │   ├── CullingPass.hpp/.cpp
│   │   ├── LodManager.hpp/.cpp
│   │   ├── Brickmap.hpp/.cpp       # Phase 7
│   │   ├── RayMarchPass.hpp/.cpp   # Phase 7
│   │   └── Renderer.hpp/.cpp
│   │
│   └── app/
│       ├── main.cpp
│       ├── Engine.hpp/.cpp
│       └── DebugUI.hpp/.cpp
│
└── tests/
    ├── test_palette.cpp
    ├── test_bitpack.cpp
    ├── test_mesher.cpp
    └── test_coords.cpp
```

Notes on deliberate choices:

- **No `include/` vs `src/` split.** That layout exists to publish headers for
  external consumers. This project builds one executable and exports nothing,
  so the split would only cost the maintenance of two parallel trees.
- **`core/Math.hpp` is the only file that includes GLM**, and
  **`platform/Window.cpp` is the only file that knows GLFW.** If either
  dependency has to be replaced, exactly one file changes.
- **Phase 7 files are listed but not yet created.** Reserving the slot keeps
  earlier work from growing into it.
- **No `external/` directory.** CPM fetches into the build tree, so no
  third-party source enters the source tree.

### 5.3 Build targets — one static library per module

```cmake
mc_core       PUBLIC  glm  Tracy
mc_platform   PUBLIC  mc_core                     PRIVATE glfw
mc_rhi        PUBLIC  mc_core                     PRIVATE glad
mc_world      PUBLIC  mc_core
mc_worldgen   PUBLIC  mc_core mc_world            PRIVATE FastNoise2
mc_mesh       PUBLIC  mc_core mc_world
mc_render     PUBLIC  mc_core mc_rhi mc_world mc_mesh
minecraft     PRIVATE (all of the above) + imgui + stb
```

Note that **`mc_render` does not link `mc_worldgen`**. The renderer has no
business knowing how terrain was produced. If someone includes
`TerrainShaper.hpp` from `Renderer.cpp`, it fails at link time. This is what
makes the dependency direction enforced rather than merely documented.

**`PRIVATE` linkage is where the enforcement actually comes from.** Linking
glad, GLFW, or FastNoise2 privately is only possible if those headers never
appear in a module's public headers. That constraint naturally produces:

```cpp
// rhi/Buffer.hpp — forces glad to become PUBLIC
class Buffer { GLuint m_handle; };        // bad

// keeps glad confined to the .cpp
class Buffer { std::uint32_t m_handle; }; // good
```

`GLuint` *is* `uint32_t`, so nothing is lost — but the compiler now guards the
integrity of the RHI seam. That matters when the Vulkan backend is eventually
added.

**Cost to accept:** cross-library inlining requires
`INTERPROCEDURAL_OPTIMIZATION ON` on every target, with `gcc-ar` / `gcc-ranlib`.
CMake handles this, but it must be set explicitly in the Release preset.
Forgetting it silently blocks inlining of the highest-frequency helpers
(`BitPack.hpp`, `Coords.hpp`) at module boundaries.

---

## 6. Conventions

### 6.1 Namespace

Flat namespace `mc` everywhere, with one exception: **`rhi` uses `mc::rhi`.**

The general rule holds — `mc::render::ChunkRenderer` would only add typing cost
for no benefit. But `rhi`'s type names (`Device`, `Buffer`, `Shader`, `Texture`,
`VertexArray`) are exactly the generic nouns most likely to collide with a
later module, so that one module earns its qualifier. Other sub-namespaces get
introduced only if a real collision appears.

### 6.2 Error handling — hybrid

Exceptions are **enabled**, but confined to initialization and loading
boundaries: shader compilation failure, missing asset, unsupported GL extension
— situations where the program cannot start at all.

**No exception is ever thrown from chunk generation, meshing, or the render
loop.** Those paths return `mc::Result<T, E>`.

`Result<T, E>` is implemented in `core/Result.hpp` because **`std::expected` is
C++23**; GCC only exposes `<expected>` in C++23 mode. The interface mirrors
`std::expected` so the migration is a type alias change.

Every worker thread entry point wraps its body in `try/catch`, captures via
`std::exception_ptr`, and hands it to the main thread. An exception escaping a
`std::jthread` calls `std::terminate`.

### 6.3 Naming

| Kind | Style | Example |
|---|---|---|
| Type | PascalCase | `ChunkMesh` |
| Function / method | camelCase | `buildMesh()` |
| Variable / parameter | camelCase | `sectionCount` |
| Member variable | `m_` + camelCase | `m_palette` |
| Constant / constexpr | `k` + PascalCase | `kSectionSize` |
| Macro | `MC_` + UPPER_SNAKE | `MC_ASSERT` |
| Namespace | lowercase | `mc` |
| File | PascalCase, matches primary type | `BinaryGreedyMesher.hpp` |
| Shader | lower_snake_case | `chunk.vert`, `cull.comp` |

Headers use `#pragma once`, not include guards.

### 6.4 Compiler settings

- `-std=c++20` (not `gnu++20`). GCC builtins are used explicitly where needed
  and do not require the GNU dialect.
- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wold-style-cast`,
  and **warnings are errors**. Centralized in `cmake/CompilerWarnings.cmake`.
- Presets: `debug` (`-O0 -g`, asserts on, sanitizers available),
  `release` (`-O3`, LTO, asserts off),
  `release-tracy` (release plus Tracy instrumentation).

### 6.5 Assertions

- `MC_ASSERT(cond)` — debug only, compiles away in release.
- `MC_VERIFY(cond)` — always active, for conditions whose check cost is
  negligible relative to the failure (GL object creation, buffer mapping).

### 6.6 Testing

**doctest**, chosen over Catch2 and GoogleTest for compile speed — it is a
single header with negligible compile-time overhead, which matters because
tests get rebuilt constantly.

Tests link individual module libraries directly, never the whole executable.
That keeps a `mc_world` test from silently depending on `mc_render`.

Priority targets for tests: palette packing/repacking, coordinate conversions,
bit packing, and mesher output correctness. These are pure functions with
sharp edge cases — exactly where unit tests pay for themselves. Rendering is
verified visually and by profiling, not by unit test.

### 6.7 Asset paths

Resolved at runtime relative to the executable location, never the working
directory. CMake symlinks `assets/` into the build directory so a debug run
from any cwd works.

### 6.8 Dependency versions

Every CPM dependency is pinned to an explicit tag or commit — never a branch.
A voxel engine's performance characteristics can shift under a silent
dependency update, and that is exactly the kind of change that must be
deliberate.

---

## 7. Phase Plan

| Phase | Content | Exit criterion |
|---|---|---|
| 0 **(done)** | CMake skeleton, GLFW + glad window, triangle | Builds, window opens |
| 1 **(done)** | Palette-compressed sections, naive culled meshing, camera | One chunk renders |
| 2 | Binary greedy meshing, texture array | Measured quad count reduction |
| 3 | Job system, streaming, frustum culling | Render distance 16, stable frame time |
| 4 | FastNoise2 terrain generation | Infinite terrain traversal |
| 5 | Indirect draw + GPU culling | Draw calls < 5 |
| 6 | Four-level LOD | **Render distance 64 at 60 FPS** |
| 7 | Brickmap far-field ray marching | Distance limit effectively removed |
| 8 | HZB occlusion, visibility graph | Underground scene performance |

Phase 6 reaches the stated performance target. Phase 7 completes the hybrid
architecture.

Every phase ends with a Tracy capture. A phase is not complete until its
performance characteristics are measured, not assumed.

### 7.1 Phase 0 result

Built and ran clean on the first configure. Verified:

- `mc_core`, `mc_platform`, `mc_rhi` and the `minecraft` executable build with
  `-Werror` and the full warning set from 6.4, with zero warnings.
- OpenGL 4.6 core context created; `GL_DEBUG_OUTPUT` active and silent.
- A shader is loaded from `assets/`, compiled, and linked.
- The triangle is drawn attribute-less from `gl_VertexID` with only an empty
  VAO bound — the same technique the chunk renderer will use for packed quads.
- Frame loop holds 60 FPS (vsync-locked at the display refresh rate).
- `mc_tests` passes.

**Frame capture.** The engine takes `--capture <path.ppm>`, which renders one
frame, reads the default framebuffer back, writes a binary PPM, and exits. This
exists because the desktop compositor refuses external screenshot tools
(`wlr-screencopy` unsupported, and X11 `import` cannot see the Wayland window).
Having capture inside the engine is better anyway: it works headlessly, it is
independent of the compositor, and it is how reference images get produced for
comparison in later phases.

PPM rather than PNG so that no image library is needed; captures are debugging
artefacts where size does not matter.

### 7.2 Phase 1 result

One 32^3 section renders, meshed and drawn from packed quads, with a free-flying
camera. 60 FPS (vsync-locked), zero warnings, 41 test cases passing.

Measured on the test section (rolling surface, four block types):

| | |
|---|---|
| Quads emitted | 4,842 of 196,608 possible faces (**2.5%**) |
| Mesh size on GPU | 37 KiB |
| Section storage | 4 bits/voxel, 5-entry palette, 16,400 bytes |

The storage number is the design working as intended: 32,768 voxels that would
be 65,536 bytes as raw BlockIds occupy 16,400 — and an untouched section costs
essentially nothing at all.

**Reversed-Z was adopted here rather than deferred.** The projection is infinite
with near and far swapped, `glClipControl` set to `ZERO_TO_ONE`, depth cleared
to 0 and tested with `GREATER`. Floating-point precision is densest near zero,
and reversing Z puts that density where distant geometry lands. There is no far
plane at all, which removes far-plane tuning from the render distance problem
entirely. Retrofitting this later would have touched the projection, the depth
state, and every shader that writes depth.

**No vertex buffer exists anywhere in the renderer.** Quads live in an SSBO and
`chunk.vert` expands each into six vertices from `gl_VertexID`, using a
per-face tangent basis chosen so that `cross(U, V)` equals the face normal —
which is what makes back-face culling correct without per-quad winding data.

Two things worth recording:

- **`packed` is a reserved keyword in GLSL.** It fails with a syntax error that
  points at the assignment, not at the name.
- The palette test caught a real defect: `fill()` released the index array but
  left the palette vector's grown capacity in place. Harmless in isolation, but
  at ~38,600 sections it is megabytes retained for nothing.

Deferred deliberately: neighbour-aware boundary culling (needs the World, Phase
3) and block textures (Phase 2). `meshSectionCulled` is kept after Phase 2
replaces it — binary greedy meshing must produce the same visible surface, so
the naive mesher becomes the oracle to diff against.

---

## 8. Open Questions

Filled in with recommended defaults above, but expected to need revisiting once
implementation makes contact with reality:

- **Actual benefit of AO-aware merging** — measure in Phase 2 and decide then.
  If the gain is negligible, drop greedy meshing for near chunks entirely.
- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- **Block texture source** — hand-authored or procedurally generated.
- **World persistence** — disk format, and whether persistence is in scope at
  all, is still undecided.

---

## 9. References

- [cgerikj/binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing) — reference implementation and timings
- [Auburn/FastNoise2](https://github.com/Auburn/FastNoise2) — node-graph SIMD noise
- [omar-owis/VoxelEngine](https://github.com/omar-owis/VoxelEngine) — GPU-driven C++23 / OpenGL 4.6 voxel engine
- [Minecraft Wiki — Noise settings / density functions](https://minecraft.wiki/w/Noise_settings) — terrain generation model
- [xCollateral/VulkanMod](https://github.com/xCollateral/vulkanmod) — chunk culling and indirect draw techniques
- [NVIDIA Research — Efficient Sparse Voxel Octrees](https://research.nvidia.com/publication/efficient-sparse-voxel-octrees) — background for the brickmap/SVO far field
- [Nick's Blog — Homebrew Voxel Engine](https://nickmcd.me/2019/10/27/homebrew-voxel-engine/)
