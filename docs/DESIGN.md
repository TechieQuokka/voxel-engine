# Voxel Engine — Design Document

> Status: **Implemented through Phase 4c.** Phases 0-3 are complete and Phase 4
> (terrain generation) is in progress; results and measurements are in section 7.
> Last updated: 2026-08-11.
>
> **The scope was widened on 2026-08-11** — see the note below. Everything above
> section 7 predates that decision and still holds; the phase plan in 7 is what
> changed.

A high-performance voxel engine written from scratch in C++20, targeting a
Minecraft-like world with an extreme render distance. Scope covers terrain
generation and the interaction needed to play in the world it generates — no
redstone and no multiplayer.

### The scope change, and why

This document originally ended the project at terrain generation: *"no redstone,
mob AI, or multiplayer — the goal is the render distance, not the game."* That was
the right call for four phases and is the reason the engine is as fast as it is.

It stopped being the right call for a measurable reason. Phase 4b and 4c added
caves, bedrock, deepslate, four stone variants, gravel and seven ores — twenty-one
block types, every one of them underground. Two play sessions were recorded, and
**neither reached any of it**: walking cannot get below the surface, and there is no
way to dig. The work is real and measured throughout section 7, and it is also
invisible to anyone who runs the program.

A renderer that can generate a world nobody can enter is a renderer with a reporting
problem, not just a missing feature. So interaction enters the scope — starting with
block placement and breaking, which is the one item that is engine work rather than
game systems, and is the prerequisite for all the others.

**What did not change:** the performance target (render distance 64 at 60 FPS), every
architecture decision in section 3, and phases 5 through 8. This widens what gets
built; it does not retire anything already decided.

**Still out of scope:** multiplayer and redstone. Mob AI, crafting and inventory are
no longer refused outright, but nothing about them is planned or designed — they are
listed in the phase plan as destinations, not commitments.

---

## 1. Fixed Constraints

| Constraint | Decision |
|---|---|
| Platform | Linux only. No cross-platform requirement. |
| Language | C++20 |
| Compiler | GCC |
| Engine | Custom renderer. No Unity/Unreal. |
| Scope | Terrain generation, plus interaction with the generated world. No multiplayer, no redstone. |
| Dependencies | As few as possible, and none that do work this project exists to do. See section 4. |
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

**Measured in Phase 2 (see 7.3): on heightmap terrain the cost is 13.5
percentage points, not a collapse.** The warning above stands as the reason to
measure, but the pessimistic figure did not materialize here. Re-check once
caves and overhangs exist.

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

> **Correction, Phase 4.** The paragraph above asks the wrong question. Minecraft
> does not evaluate its density function per voxel: `noise_settings` exposes
> `size_horizontal` and `size_vertical`, which define an interpolation cell of
> `4 * size` blocks per axis, and the Overworld uses 1 and 2 — so one sample covers
> 4x8x4 = 128 blocks and the rest is trilinear interpolation. This engine now does
> the same (`worldgen/DensityField`), which turns a 393,216-evaluation column into
> 3,969 samples: a factor of **99**.
>
> The library choice stands, and the measured throughput above is still the reason
> for it. But the conclusion that noise throughput was the bottleneck did not
> survive contact with how Minecraft actually works — the win was in not calling the
> noise function, not in calling a fast one. Interpolating is also part of the
> *look*, because smoothing the density field vertically is what gives voxel terrain
> its layered feel instead of the blobbiness of per-voxel 3D noise.

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
├── CMakePresets.json          # debug / asan / tsan / release / release-tracy
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
│   │   ├── Neighbourhood.hpp  # the 3x3x3 sections a mesher may read
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
- Presets: `debug` (`-O0 -g`, asserts on), `asan` (debug plus
  address/undefined), `tsan` (debug plus thread), `release` (`-O3`, LTO, asserts
  off), `release-tracy` (release plus Tracy instrumentation).
- **`tsan` is not optional for the job system.** A lock-free queue whose tests
  pass has demonstrated nothing about its memory ordering; only the sanitizer
  can say. Any change to `MpmcQueue` or `JobSystem` gets re-run under it.

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

### 6.9 Colour management — shading is linear

**All shading arithmetic happens in light-linear space.** Concretely:

- Block textures are `GL_SRGB8_ALPHA8`, so the sampler decodes on fetch and
  mipmap generation averages in linear space. `rhi::ColorSpace` is a required
  argument of `TextureArray::create`, not a default — data that is not a colour
  (brickmap indices in Phase 7) must not be decoded.
- `GL_FRAMEBUFFER_SRGB` is enabled for the lifetime of the context, so the
  hardware encodes on write. **Colours handed to `Device::clear` are therefore
  linear**, and the sky colour is decoded through `rhi::srgbToLinear` at the
  call site rather than written as a pre-converted magic number.
- Shading factors chosen by eye — the per-face brightnesses, the AO ramp — are
  *perceptual* quantities. They stay written as the sRGB values they were tuned
  as, and are decoded where they are used: `srgbToLinear()` on literal arguments
  in `chunk.frag` (constant-folded), and the pre-decoded `kAoLinear` table in
  `chunk.vert`, because GLSL forbids function calls in a `const` initializer.

The AO levels are decoded per-vertex rather than per-fragment for two reasons:
interpolating occlusion across a merged quad is only meaningful in linear space,
and a table lookup is free where a per-fragment `pow` would not be.

Multiplying sRGB-encoded values, which is what the shader did through Phase 2,
is simply the wrong operation for quantities that behave like light. Decoding the
tuned constants keeps the rendered result within ~3/255 of the old appearance
(worst case ~7/255, in deep shadow, slightly darker), so this was a correctness
fix rather than an art change. The exact piecewise sRGB curve is used, not a 2.2
power, because it has to agree with the hardware's encode.

---

## 7. Phase Plan

| Phase | Content | Exit criterion |
|---|---|---|
| 0 **(done)** | CMake skeleton, GLFW + glad window, triangle | Builds, window opens |
| 1 **(done)** | Palette-compressed sections, naive culled meshing, camera | One chunk renders |
| 2 **(done)** | Binary greedy meshing, texture array | Measured quad count reduction |
| 3 **(done)** | Job system, streaming, frustum culling | Render distance 16, stable frame time |
| 4 *(in progress)* | FastNoise2 terrain generation | Infinite terrain traversal |
| 5 | Indirect draw + GPU culling | Draw calls < 5 |
| 6 | Four-level LOD | **Render distance 64 at 60 FPS** |
| 7 | Brickmap far-field ray marching | Distance limit effectively removed |
| 8 | HZB occlusion, visibility graph | Underground scene performance |

Phase 6 reaches the stated performance target. Phase 7 completes the hybrid
architecture.

**Interaction track**, added 2026-08-11 with the scope change. Numbered after the
performance phases but **not sequenced after them** — the two tracks are independent,
and every phase built since the scope change has come from this one.

| Phase | Content | Exit criterion |
|---|---|---|
| 9 **(done)** | Voxel raycast; block placement and breaking | A player can dig from the surface into a cave |
| 10 | Vegetation — non-cube geometry, second mesher path | Flowers and grass on the surface |
| 11 | Persistence — chunk save format | An edited world survives a restart |
| 12 **(done)** | Block updates — neighbour notification and scheduled ticks | Sand falls when its support goes |
| 13 **(done)** | Entities — the first thing in the world that is not a voxel | A broken block drops an item |
| 14 **(done)** | Inventory and a HUD | That item can be picked up and placed again |
| 15 **(done)** | Health — hearts and fall damage | A fall costs something |

**14's exit criterion was claimed and was not met**, and it took four play sessions
and a set of counters to find out. An item could be dropped and placed; it could not
be *picked up*, because the reach was measured from the eye and an item at the feet
is 1.50 away against a radius of 1.4. Fixed in 7.14 — and the phase table is where
that matters, because a phase marked done against an exit criterion nobody exercised
is exactly how it happened.

**15 landed inside 7.12** rather than as a phase of its own, which is why it appears
here after the fact: hearts and fall damage were asked for alongside the inventory
and shipped in the same commit. `Engine.hpp` has named it Phase 15 since then.

**Trees landed early, in 7.9, and did not wait for Phase 10.** Vegetation is priced
as a second mesher path because grass and flowers are cross-quads; a log and a leaf
are cubes, so a tree costs two block types and a feature and nothing else. The phase
still exists for everything that is genuinely not a cube.

Phases 12 to 14 were the subsystems this engine did not have, listed in the order
their dependencies force. **13 and 14 landed together** (7.10) because separately each
is half a feature: a drop nobody can pick up is scenery, and an inventory with nothing
to put in it is an empty array. **12 landed last** (7.11), and with it all three exist.

**Water landed on 2026-08-12 and never became a phase of its own**, which was the
right call for two of its three parts and a scoping decision for the third. Water
against water face culling and the translucent second draw pass are built; **aquifers
are not**, so there are oceans and no flooded caves. RESEARCH.md 5.3 has the original
statement of the three problems and 7.13 has what was done about them. What 12 had
already removed from the list is the fourth item — the tick to flow on — which exists
and is the same one falling sand uses; **flowing water still does not**, and the note
at `BlockUpdates::examine` says why it is not a small addition.

Phase 9 was deliberately first of the three, and both halves of that argument held.
It reused the dirty mask, the remeshing path, `Palette::set` and the light recompute
rather than adding machinery beside them, and it added nothing to the streaming
pipeline at all. The raycast it produced for aiming turned out to be what fixed the
third-person camera clipping through terrain, which had been an open limitation
since the character landed. Results in 7.8.

Anything past 11 — inventory, crafting, entities — is a destination, not a plan.
Nothing here commits to it, and each would be larger than this repository is today.

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

### 7.3 Phase 2 result

Binary greedy meshing and the block texture array. Measured on the same test
section, release build with LTO, mean of 200 runs:

| Strategy | Quads | Reduction | Mesh time |
|---|---|---|---|
| Culled (reference) | 4,842 | — | 343.1 us |
| **Greedy + AO-aware merge** | **2,083** | **57.0%** | **184.4 us** |
| Greedy, AO ignored | 1,428 | 70.5% | 182.1 us |

#### The AO question, answered

Section 3.6 warned that AO-aware merging could collapse the benefit to ~15%,
citing published measurements, and flagged this as the most fragile assumption
in the design. **On this terrain it does not.** AO-aware merging keeps 57% of
the reduction against 70.5% when AO is ignored — a real cost, but nothing like
the collapse that was feared.

**Decision: keep AO-aware merging for near chunks.** Losing 13.5 percentage
points of merging to keep correct ambient occlusion is a good trade at close
range, where AO is what makes voxel geometry read as solid. Distant LOD levels
still turn it off, where the extra merging is free because AO is not
perceptible there.

This number is specific to smooth heightmap terrain with few materials. It
should be re-measured once real worldgen produces caves and overhangs (Phase 4),
where AO varies far more per face.

#### Greedy meshing had to be made fast twice

The first implementation produced the right quad counts but was **1.5x slower
than the reference mesher** (517 us vs 343 us). The bitwise face culling was
correct, but the merge step then walked all 6 x 32 x 32 x 32 = 196,608 plane
cells to find the ~4,800 that actually held a face — discarding exactly the
advantage the masks were built to provide.

Rewriting the scatter to iterate only set bits (`countr_zero` over each mask
word, clearing with `bits &= bits - 1`) and to skip empty planes brought it to
184 us: **1.86x faster than the reference mesher**, on top of producing 57%
fewer quads.

The lesson generalizes: building the bitmask is not the optimization. Never
touching the empty cells is.

#### Texture array

`GL_TEXTURE_2D_ARRAY` with `GL_REPEAT`, 16x16 RGBA8 layers, mipmapped with
anisotropic filtering and nearest magnification. The array is what makes merged
quads work at all: `chunk.vert` emits UVs running 0..width and 0..height so a
merged quad tiles its texture once per block. An atlas cannot do that — its
tiles need padding against bleeding, and padding forbids wrapping.

The `material` field of a Quad therefore holds a **texture layer, not a
BlockId**. Grass is the case that forces this: one block type, three layers
(top, side, bottom).

Textures are **generated procedurally** rather than loaded from files. This
settles one of the open questions from section 8: it keeps binary assets out of
the repository, the output is deterministic, and it avoids an image-loading
dependency entirely. Swapping in authored PNGs later changes only how the pixel
buffer is filled.

#### Layering fix

`Face` moved from `mesh/Quad.hpp` to `world/Coords.hpp`. `BlockRegistry` needs
it to answer `textureLayer(block, face)`, and `world` including a `mesh` header
would have inverted the dependency. A face direction is a property of the world
grid, not of any particular meshing strategy.

Verification: the equivalence tests assert that greedy and reference meshing
cover the *same cells*, not merely the same total area — an area-only check
would pass if a quad were displaced with a compensating error elsewhere.

### 7.4 Interim cleanup, before Phase 3

A full read of the tree before starting Phase 3 turned up four things worth
fixing while they were still cheap. None of them changed the architecture.

**The two meshers disagreed about the `material` field.** `CulledMesher` wrote a
`BlockId` where `BinaryGreedyMesher` wrote a texture layer — so the reference
mesher's output, despite 7.3 documenting the field as a layer, would have
rendered with the wrong textures. The equivalence test recorded a material per
covered cell but never compared it, which is why nothing caught this. It now
compares the value, so the two meshers are locked to one convention.

**sRGB was half-configured.** The window asked for an sRGB-capable framebuffer
and nothing used it. Resolved as described in 6.9.

**The meshing benchmark ran on the startup path.** Several hundred meshing passes
before the first frame. Moved behind `--mesh-benchmark`; the code stays, because
the AO merge measurement has to be repeated in Phase 4 against terrain with
caves and overhangs.

**Duplicated tuning constants.** FOV and near plane were written twice, in the
constructor and in the resize handler, where they could drift apart; both now go
through `Engine::updateProjection`. The fragment shader's distance-darkening
range was hard-coded at 400 blocks and is now `ChunkRenderer::setFadeDistance`,
for streaming to derive from the render distance in Phase 3.

Verified: 53 test cases pass, `-Werror` clean, and a `--capture` frame renders
identically apart from the sRGB change. The Phase 2 measurement reproduces
exactly — 4,842 → 2,083 quads AO-aware (57.0%), 1,428 AO-ignoring (70.5%).

### 7.5 Phase 3 result

**Exit criterion met.** Render distance 16 holds a p99 frame time of 0.85 ms — twenty
times under a 60 FPS budget — and distance 24 holds 2.14 ms.

> **The numbers in this section were re-measured after Phase 4a.** The benchmark that
> produced the original ones was broken in two ways, both found by running a longer
> flight and noticing that the last frame drew nothing. See 7.7.

Phase 3 was built in sub-steps, each verifiable on its own. The job system was wired
in **last**, deliberately: getting streaming correct single-threaded first meant a
later bug was either a streaming bug or a race, and not ambiguously both. That paid
off — 3f changed no rendering behaviour, and the capture it produces is pixel-identical
to the single-threaded one.

| | Content | State |
|---|---|---|
| 3a | `core/MpmcQueue`, `core/JobSystem` | done |
| 3b | `world/Chunk`, `world/World`, `world/Neighbourhood`, `worldgen/Generator` | done |
| 3c | Neighbour-aware boundary culling and AO | done |
| 3d | Multi-chunk rendering, per-section data in an SSBO | done |
| 3e | `render/Frustum`, hierarchical culling | done |
| 3f | Job system wired in, upload thread | done |

**3a.** Vyukov bounded MPMC queue plus a semaphore-parked worker pool. Bounded is
the point: an unbounded queue would let streaming enqueue more work than the pool
can retire, so pending-job memory would scale with camera speed. `Job` is a POD
(function pointer, context, `u64`) rather than `std::function`, both because `core`
must not know what a section is and because a lambda capturing `World*` plus
`SectionPos` overruns libstdc++'s 16-byte buffer and would allocate per section.

**3b.** Columns are held by `unique_ptr` because jobs hold a `Chunk*` across
frames and a value-holding map would invalidate every one of them on rehash. A
column in `Generating` is never unloaded. `worldgen` exists now with a placeholder
analytic heightmap — continuous in world coordinates, so a seam means the neighbour
handling is wrong — and Phase 4 replaces only its body.

Measured, release + LTO, RTX 3060 machine, 8 hardware threads:

| | Distance 8 | Distance 16 |
|---|---|---|
| Columns | 289 | 1,089 |
| Region update | 0.14 ms | 0.28 ms |
| Generation, 1 thread | 116.8 ms (0.404 ms/column) | 435.3 ms (0.400 ms/column) |
| Generation, 6 workers | 29.0 ms (4.03x) | 109.6 ms (3.97x) |
| Resident memory | 4.4 MiB | 16.7 MiB |

Three things worth keeping:

- **16.7 MiB for a full distance-16 world** is the uniform-section optimization
  paying off exactly as 3.5 predicted. Terrain occupies a narrow height band, so
  nine or ten of every twelve sections carry no index array at all.
- **4x on 6 workers, not 6x.** The placeholder generator is memory-bound — its
  inner loop is `Palette::set`, a palette scan plus a read-modify-write per voxel —
  so it saturates bandwidth before it saturates cores. Phase 4's density evaluation
  is compute-bound and should scale better; if it does not, the generator writes
  through the palette one voxel at a time and that is the thing to fix.
- **Gathering a neighbourhood costs 0.050 us per section**, against ~180 us to mesh
  one. Nine hash lookups shared down the y axis, and nothing needs caching.

**3c.** Both meshers now take a `SectionNeighbourhood` instead of a lone `Section`,
and `emitBoundaryFaces` is gone — it was a flag standing in for neighbours that did
not exist yet, and the situation it described is now expressed by supplying them.

The opacity grid inside the mesher is padded to 34³, so a neighbour lookup is an
array read rather than a branch on which section a coordinate falls in. One layer of
padding is exactly enough, which is worth stating because it is not obvious: face
culling reaches one voxel along the normal, and AO reaches one along the normal *and*
one along each tangent, but the tangents are perpendicular to the normal, so those
offsets never stack onto the normal's axis.

Boundary culling then falls out of the existing bit trick. The shift in
`cx & ~(cx << 1)` used to bring in a zero at the end of the column, which *was* the
"outside is air" rule; the neighbour's real occupancy is shifted in there instead.
The shell is filled per neighbour rather than per voxel, so a null or uniform-air
neighbour costs nothing beyond the initial memset — the common case, since most of a
column is sky.

Measured on a distance-8 world, 892 non-empty interior columns' sections (interior
only, so every one has all 26 neighbours), release + LTO:

| | Isolated | With neighbours |
|---|---|---|
| Quads | 229,263 | 205,628 (**-10.3%**) |
| Covered area, unit faces | 4,879,812 | 562,181 (**-88.5%**) |
| Meshing | 164.8 us/section | 115.8 us/section (**-29.7%**) |

**Quad count is the wrong metric for this change, and the gap is instructive.** A
seam wall is a flat 32x32 plane, which greedy meshing already collapses into one or
two quads — so counting quads makes the redundant geometry look like a rounding
error while it is in fact 88% of the drawn surface. That is 8.7x the fill rate, on
surfaces that are by construction invisible.

Meshing also got *faster*, not slower. The extra shell decode is more than repaid by
the scatter and merge passes having 88% less surface to walk. This is the same lesson
as Phase 2's: the cost is in touching cells, so the work that avoids touching them
pays for itself.

Both AO tests were verified by sabotage — neighbour sampling in `computeAo` was
temporarily restricted to the centre section, and exactly those two tests failed
while the other 109 passed. A first draft of the seam test had passed for the wrong
reason: it used `quad.width()` where `PosY` merges x along `height`, which made its
condition trivially true.

**3d and 3e.** The whole visible set is drawn with **one** `glMultiDrawArrays` and
**no per-draw uniforms at all**. Two OpenGL properties make that possible and both
are easy to get wrong:

- `gl_VertexID` is absolute — it already includes the draw's `first` — so
  `gl_VertexID / 6` indexes the shared quad arena directly, with no base offset
  passed in.
- `gl_DrawID` gives each draw its index into the multi-draw list, which is how a
  section finds its own origin in a second SSBO. This is why the per-section data is
  laid out as an array indexed by draw: Phase 5 replaces the CPU-filled command list
  with a GPU-filled indirect one and **the shader does not change**.

Section meshes live in one persistently mapped arena, suballocated by
`core/RangeAllocator` (first-fit, coalescing, no GL in it, so its edge cases are
unit-testable). Freed ranges are withheld for three frames before reuse: a coherent
mapping orders writes, it does not know what the GPU is still reading. Nothing is
ever overwritten in place — an updated mesh always takes a fresh range — so three
frames of delay is the whole of the synchronisation.

The frustum has **five planes, not six.** The row of an infinite reversed-Z matrix
that would give the far plane is `(0, 0, 0, near)` — a zero normal — so the textbook
six-plane extraction normalizes it, divides by zero, and produces a plane that
rejects the entire world. There is nothing too distant to draw, which is the point of
the infinite projection. Culling is hierarchical: one test against a 32x384x32 column
box rejects twelve section tests.

Measured, release + LTO, RTX 3060, distance 16, 900 frames with vsync off and the
camera flying forward at 40 blocks/s so streaming is included:

| | |
|---|---|
| Warm-up (whole region, 1 thread) | 0.86 s — 1,089 columns, 3,812 sections |
| Frame time | mean 1.17 ms, **median 0.35 ms**, p99 **29.7 ms**, max 33.2 ms |
| Drawn, typical frame | 512 sections, 160,292 quads, **1 draw call** |
| Culled, same frame | 782 columns + 2,344 sections |
| Arena | 7 MiB used of 51 |

**The exit criterion is not met, and the numbers say precisely why.** A median of
0.35 ms means rendering 160,000 merged quads with hierarchical culling is nowhere
near the budget — it is 2% of a 16.7 ms frame. Every millisecond of the p99 is the
synchronous streaming: crossing a column boundary generates up to 16 columns
(0.40 ms each) and meshes up to 32 sections (0.12 ms each) inside one frame. That is
what 3f moves off the main thread, and it is worth having measured before fixing,
because the obvious suspect — draw submission — turns out to be irrelevant.

The arena is sized from that measurement rather than guessed: ~11 KiB of quads per
column, taken to 48 KiB for margin, which is 51 MiB at distance 16. It was 192 MiB
fixed, and since the arena is pinned memory whether used or not, scaling it to the
render distance is not a micro-optimization.

Distance fog changed from a multiply toward black to a `mix` toward the sky colour.
The multiply was a Phase 1 stand-in and it stopped being defensible once shading
moved to linear space: a linear ramp to zero reads as terrain turning black at the
render distance, which looks like a bug. Blending toward the sky is what fog
physically is — light scattered in, not light removed — and it makes the edge of the
loaded region disappear rather than announce itself.

**3f.** Generation and meshing moved onto the worker pool and uploads onto their own
thread, and the frame loop now only *submits*. It never waits for a worker: whatever
the pool has not finished simply appears a frame or two later.

**The upload thread needs no GL context**, which corrects what 3.13 was assumed to
imply. The arena is persistently and coherently mapped, so writing to it is a memcpy
into process memory and issues no GL command at all — any thread may do it. The whole
GL-side contract is one `barrierAfterClientWrites()` per frame on the context-owning
thread; coherence removed the need to flush, not the need to order. A second shared
context, with its fence and object-visibility rules, would have been complexity bought
for nothing.

The real hazard turned out to be lifetime, not GL. A meshing job borrows
`const Section*` from up to nine columns and holds them across frames, so if the camera
moved far enough the World would unload one underneath it. `Chunk::pin()` answers that:
a counter, not a flag, because one column is a neighbour of nine sections and can be
pinned by nine jobs at once. The pin is held until the *upload* finishes, not until the
mesher returns, which also closes the smaller hole where a section could be stored after
its column was already released.

Work is passed by pooled index rather than by value: `Job` carries a `u64`, and a pooled
`ChunkMesh` keeps its vector capacity between uses, so once the pool is warm the mesh
path allocates nothing. Ownership moves main → worker → upload → free list, and the
queue's acquire/release pairs are what make each handoff safe.

Measured, release + LTO, 6 workers plus an upload thread, 900 frames with vsync off and
the camera flying at 40 blocks/s:

| Render distance | 8 | 16 | 24 |
|---|---|---|---|
| Columns | 289 | 1,089 | 2,401 |
| Sections meshed | 892 | 3,812 | 8,748 |
| …holding geometry | 535 | 2,280 | 5,228 |
| …fully enclosed, empty | 357 | 1,532 | 3,520 |
| Warm-up | 0.06 s | 0.24 s | 0.56 s |
| Frame mean | 0.17 ms | 0.41 ms | 0.86 ms |
| Frame median | 0.11 ms | 0.36 ms | 0.83 ms |
| **Frame p99** | **2.22 ms** | **2.08 ms** | **1.76 ms** |
| Quads drawn | 1,615 | 150,733 | 469,294 |
| Arena used | 1 MiB | 7 MiB | 15 MiB |

Against 3d/3e at distance 16, p99 went from 29.7 ms to 2.08 ms and warm-up from 0.86 s
to 0.24 s. Two things in that table are worth more than the headline:

- **Nothing is left generating at the end of any run.** That is the claim "streaming
  keeps up" reduces to, and it is now checked rather than assumed — the benchmark warns
  when the backlog exceeds one region's worth, because frame times measured against a
  world with holes in it mean nothing.
- **Two fifths of all meshed sections produce zero quads.** They sit entirely inside
  rock, every face hidden by a neighbour, so they are meshed once and stored nowhere.
  This number only exists because 3c made boundary culling real; before it, every one
  of those 1,532 sections would have emitted six full walls.

Verification. TSan reports **zero** races over the full pipeline, including a
400-frame run with the camera moving so that columns load and unload while jobs hold
pins — the path `Chunk::pin()` exists for. GTK, dragged in by GLFW's Wayland backend,
races inside itself during window creation in a different library each time; `tsan.supp`
suppresses those by library, and the application run additionally sets
`report_mutex_bugs=0`, because engine code uses `std::lock_guard` exclusively and cannot
produce that class of report. The unit-test run links no GTK and keeps every check on,
and it is the run that exercises `MpmcQueue` and `JobSystem` directly.

A `--capture` frame from the async pipeline is pixel-identical to the synchronous one,
and the meshed-section count matches exactly (3,812 both ways). The `release-tracy`
preset builds and runs; taking an actual Tracy capture needs the Tracy server GUI, so
that step is left to a human.

### 7.6 Phase 4 progress

Before starting, Minecraft's own generation pipeline was researched rather than
assumed. Two findings changed the plan; both are recorded where they belong (the
interpolation grid in the correction to 4.1, the pipeline order below).

| | Content | State |
|---|---|---|
| 4a | FastNoise2, density grid, terrain shaping, surface pass | done |
| 4b | Noise caves (cheese / spaghetti / noodle) | done; aquifers not yet |
| 4c | Ore features, with the air-exposure rule | |
| 4d | Biome selection from the climate fields | |

**Generation is a pipeline, not a step.** Java Edition orders it
`biomes → noise → surface → carvers → features → light`, and the order is load
bearing: ores are a `features` entry that runs *after* `carvers`, which is the only
reason ore's `discard_chance_on_air_exposure` — the rule that stops diamond lying
around on cave walls — means anything. `Generator` is built in these stages for that
reason, and 4b has to land before 4c.

**4a.** `worldgen/DensityGraph` holds the FastNoise2 node graphs; FastNoise2 is a
PRIVATE dependency and appears in no header, so the noise backend or a future GPU
evaluation changes one file. `worldgen/DensityField` is the interpolation grid and
contains no FastNoise2 at all, so its indexing and interpolation are unit-testable —
which mattered, because a transposed grid index is invisible in code and shows up as
terrain made of floating horizontal sheets.

The channels follow the noise router: continentalness and erosion through
piecewise-linear splines (Minecraft nests cubic splines three deep; linear gets the
same *shape* and is far easier to debug), peaks-and-valleys, and a vertically squashed
3D field that warps the result so overhangs are possible.

Two things were got wrong first and are worth recording:

- **FBm cannot make a mountain.** It is symmetric about zero, so raising its amplitude
  gives taller rolling hills and never a ridge. Peaks-and-valleys had to become a
  *ridged* fractal, which folds the absolute value and puts a crest where the
  underlying noise crosses zero — the same shape Minecraft reaches by folding
  weirdness.
- **Terrain was tuned by measurement, not by eye.** A `--transect` style probe reports
  height range, standard deviation and local relief over 8,000 blocks. The first
  parameter set gave a range of 38 blocks and a relief of 14 over any 128 — terrain
  that reads as a contour map. The current set gives range 76, stddev 14.3, relief 47,
  against a target of ~100 and ≥40 taken from what Minecraft does between plains and
  mountains.

The camera spawn now queries the surface height. A fixed spawn height was fine while
terrain never rose above y=60; with a real density field the ground at the origin is
at y=95, so the first render of Phase 4 was taken from inside a hill — and what that
looks like on screen is not recognisable as a spawn bug.

Measured, release + LTO, 6 workers, FastNoise2 dispatching to AVX2:

| | Distance 8 | Distance 16 |
|---|---|---|
| Warm-up | 0.24 s | 0.85 s |
| …placeholder generator, for comparison | 0.06 s | 0.24 s |
| Sections meshed | 1,156 | 4,967 |
| …holding geometry | 565 | 2,458 |
| …fully enclosed, empty | 591 | 2,509 |
| Frame p99 | 0.55 ms | 0.85 ms |
| Arena used | 2 MiB | 10 MiB |

Real terrain costs about 3.5x the placeholder to generate and still streams inside the
frame budget, with nothing left generating after 20 seconds of flight. Note that the
fully-enclosed section count has already fallen as a share (2,509 of 4,967 versus
1,532 of 3,812) because ridged terrain has more surface per column; caves in 4b will
push it down further, and the arena sizing in `meshArenaBytesFor` should be rechecked
then.

### 7.7 The benchmark was measuring the wrong thing

Worth its own section, because every frame-time number in 7.5 and 7.6 came from it and
because the failure was silent in exactly the way a bad instrument usually is.

The symptom appeared only on a longer run: after 3,000 frames at distance 8 the report
said `0 sections drawn, 0 quads`, and the loaded set had grown to 357 columns where the
region asks for 289, with 162 of them still generating. Two independent bugs.

**The camera flew along its view direction.** The spawn pitch is -0.18, so 2,000 blocks
of travel sank it 358 blocks and out through the bottom of the world. Every run had been
measuring an increasingly underground view. Fixed by moving along the horizontal
projection of forward and following the terrain height, read from already-loaded chunks
rather than from the generator — calling `surfaceHeight` per frame would have put terrain
generation inside the thing being measured.

**The camera advanced by a fixed 1/60 s step while frames ran as fast as they could.** At
5,000 FPS that is 83x real speed: 20 seconds of simulated motion in 0.4 s of wall time.
Streaming could not possibly keep up, so the world had holes and the visible set emptied.
This biases the two halves of the measurement in *opposite* directions — streaming
submission far heavier than reality, rendering far lighter — which makes the result
uninterpretable rather than merely wrong. The benchmark is now duration-based
(`--bench-seconds`) and advances the camera by measured delta time, so 40 blocks/s means
40 blocks per real second.

Correcting it moved distance-16 p99 from 2.08 ms to 0.81 ms, and the numbers are now
attached to a full visible set (716 sections, 260,000 quads) rather than a sparse one.

Three lessons, all recorded in HANDOFF.md:

- **A benchmark needs its own sanity check.** The run now reports how far the camera
  actually travelled and how many columns are still generating, and warns when the
  backlog exceeds a region. Both would have caught this immediately.
- **Delta time is easy to get wrong in a way that still runs.** Reading the clock at the
  top of the loop while updating `previous` at the bottom measures the gap *between*
  iterations, not the frame — nearly zero. The first attempt at the fix did exactly this
  and moved the camera 11 blocks in 20 seconds.
- **A measurement that flatters you deserves more suspicion than one that does not.**
  The p99 improved when the instrument was fixed, but it improved for a checkable
  reason.

---

### 7.8 Phase 9 result — block placement and breaking

The first phase of the interaction track, and the first time anything in this engine
writes to the world after generation.

| | Content | State |
|---|---|---|
| Voxel raycast | Amanatides & Woo DDA over the block grid | done |
| Break / place | left and right mouse, edge-triggered | done |
| Remesh + relight | through the existing dirty mask, no new machinery | done |
| Selection outline | 24 vertices, one draw call, no buffer | done |
| Third-person camera collision | second caller of the raycast | done |

**Nothing new was added to the streaming pipeline.** An edit marks sections dirty
and the existing scheduler picks them up on the next frame, which is the whole
reason this phase was chosen to go first — see the phase plan in section 7.

#### Editing without a lock

The hazard is not the write, it is who might be reading. A meshing job borrows
`const Section*` into nine columns and holds them across frames, and `Palette::set`
can reallocate the index array when the palette outgrows its bit width — so writing
under a reader is a use-after-free, not a torn read.

The answer already existed: `Chunk::pin()`. Every reader of a column pins it, and a
job meshing a *neighbouring* section pins all nine of its columns, so one
`pinned()` test on the edited column covers every reader there can be.
`World::setBlock` returns `Busy` rather than blocking, and the caller retries next
frame. Pins sit at zero in a steady state, so in practice a retry never happens; the
engine counts how many frames an edit has waited and complains past 20, because an
edit that never lands means a leaked pin somewhere else.

#### What an edit invalidates, and what it does not

Three things, and the third is the one that is easy to get wrong:

1. The section holding the block.
2. Every section the block **touches**. The mesher's AO reads a 3x3x3 of voxels, so
   a block in a section corner changes shading in up to seven neighbours — including
   across a column boundary.
3. Wherever the sky light moved. Light is part of the mesher's merge key and the
   padded light grid reaches one voxel into the adjacent column, so a light change
   has to dirty the same section in the **eight surrounding columns** too.

Point 3 is why `computeSkyLight` now returns a mask of the sections it actually
changed. Without that signal the honest implementation is "relight the column and
remesh nine columns on every click", and most of that work would be for sections
whose light did not move. **Breaking a block underground changes no sky light at
all** — it is already zero down there — so the common case of digging costs one
section, and the mask is what says so. A test pins exactly this.

Relighting is a full column recompute rather than an incremental flood fill.
Measured at about 0.5 ms, on a path that runs per click and not per frame, against a
vertical fill that depends on a heightmap one block can move by any amount. An
incremental relight is the optimisation to reach for if it ever shows up in a
profile, and not before.

#### Measurements

Warm-up at distance 16 went from 3.58 s to **3.40 s**, which was not the goal and is
worth explaining rather than claiming. The change-detection rewrite of the light
store pass stopped it clearing each mixed section to zero and then re-expanding the
nibble array on the first non-zero write; comparing in place does no allocation at
all. Detecting change came out slightly cheaper than not detecting it.

Frame time did not move in a way this benchmark can resolve. Three consecutive runs
at distance 16 measured p99 6.37, 6.94 and 7.98 ms, mean 3.92 to 4.78 — a spread
wider than anything one 5-block raycast per frame could account for, and wider than
the gap to the 5.91 ms recorded for 4c. **That earlier figure was a single run**, so
comparing one sample against three is not evidence either way; what can be said is
that the added per-frame cost is one DDA march of at most a few dozen voxel lookups,
plus one draw call of 24 vertices when something is in reach.

#### The third-person camera, closed

7.6 recorded that the third-person camera clipped through terrain and that fixing it
needed a voxel raycast the engine did not have. It has one now, and the fix is four
lines: cast backwards from the eye, stop short of what is hit. This is the second
caller of the raycast and the reason Phase 9 was ordered ahead of vegetation — the
prerequisite argument was not hypothetical.

---

### 7.9 Trees, break time, and a walking fix

Four items raised after the first real play session, grouped because none of them
needs a subsystem that does not exist. That was the selection rule, not the theme.

#### Walking a full block was wrong, and it was one constant

`kStepHeight` was 1.05, so every rise in the world was walked up automatically. That
is not how Minecraft behaves: **vanilla steps up at most 0.6 of a block**, which
covers slabs and stairs and nothing else, and stepping a whole block without jumping
is a *mod*. Since every block here is full height, 0.6 means no rise is ever walked
and all of them are jumped — which is what the real game feels like and what was
reported as missing. The jump already cleared it: 8.5 m/s against 28 m/s² peaks at
1.29 blocks.

Worth recording as a category. This was not a missing feature, it was a fidelity bug
that had been in since walking landed, and it took a player thirty seconds to feel
something that no test would ever have caught.

#### Trees are much cheaper than "vegetation"

RESEARCH.md 5.4 priced vegetation as a second mesher path — no greedy merge,
back-face culling off, alpha testing on. **That price is for grass and flowers,
which are cross-quads. It is not the price of a tree.** Logs and leaves are cubes.
Making leaves opaque, exactly as vanilla's "fast graphics" does, means trees cost:
two block types, three texture layers, one feature. No mesher change, no new render
path, no streaming change.

The one real cost is a seam, and it is a different seam from the blob features':

> A blob's geometry is a pure function of (seed, column, feature, attempt), so a
> column can work out where its neighbour's blobs went without asking anyone. **A
> tree stands on the ground**, so its height depends on terrain at the trunk — and
> when column T replays column S's trees, T has no way to know how high S's ground
> is. That is a world read across a border, which is the ordering dependency the
> whole streaming design exists to avoid.

So trees do not cross columns: a trunk is inset far enough that its canopy fits
inside its own column. **The cost is a two-block band along each column edge where no
tree grows**, leaving 28x28 of each 32x32 column plantable. It is a real artefact —
a faint grid of tree-free strips — and it is the same class of deliberate compromise
as the air-exposure rule treating outside the column as solid. The alternative is
vanilla's chunk-status pipeline, which is a much larger thing than trees.

Measured at 4.6 trees per column, about 62 leaves each, which is sparse woodland —
between vanilla's plains and its forest. Without biomes the density has to be one
number everywhere, so it is deliberately on the thin side: uniform thick forest reads
worse than uniform light woodland.

#### Breaking takes time, and the overlay is most of the work

Hardness is one field per block, sourced from the game's own data (RESEARCH.md 8).
The formula and the decision not to gate on tools are both there; the short version
is that reproducing vanilla exactly would make every ore unobtainable, because bare
hands cannot harvest them at all.

**The crack overlay is the part that mattered.** Holding a button for four seconds
with no feedback is worse than an instant break — the player cannot distinguish
"mining" from "nothing is happening". Ten destroy stages, generated procedurally like
every other texture here, drawn as a blended cube over the target. Two details:

- The stages are **cumulative**: branch *k* follows the same path at every stage and
  a stage merely draws more branches, so cracks spread rather than being reshuffled
  ten times. Ten unrelated patterns read as flicker, not damage.
- The first version wrote the crack colour as `12/255` thinking it near-black, and it
  came out mid-grey, because `GL_FRAMEBUFFER_SRGB` encodes on write and that value is
  *linear*. `2/255` is the dark this wanted. Same rule as `Device::clear`, third time
  it has caught someone — see 6.9.

Progress resets whenever the crosshair leaves the block, which is vanilla's rule and
what stops a player chipping four blocks at once by sweeping across them.

#### What this did not touch

Water, falling sand and item drops were all asked for at the same time and are all
absent, on purpose. Each needs one of the three subsystems this engine does not have
— block updates, entities, a UI layer — and mixing "one constant" with "build an
entity system" in one change would have meant neither landing cleanly. Section 8
lists them.

---

### 7.10 Phases 13 and 14 — entities, drops, and a HUD

Asked for after the second play session, and built together because separately each
is half a feature: a drop nobody can pick up is scenery, and an inventory with
nothing to put in it is an empty array.

#### The mining swing, and why first person was the real work

Third-person was nearly free. `CharacterRenderer` already had per-box pivots and a
swing amplitude for the walk cycle, so the chop is one flag on the right arm's two
boxes and a `mix` between the walk angle and an arc. Every other limb keeps walking,
which is what mining while moving looks like.

**First person needed a view model, and that is new.** The arm is built in world
space against the camera basis rather than in a second view-space projection —
placing it a fixed offset from the eye means it reuses the character shader, quad
format and buffer exactly, with no second projection matrix to keep in step with the
first. What it does need is `Device::clearDepth` before it draws, or standing against
a wall buries the arm inside the terrain. Minecraft clears depth for the same reason.

The swing is driven by **time, not by break progress**. Tying it to progress would
run the animation at a different speed for dirt and for deepslate, and stop it dead
on bedrock, which cannot be broken at all. The arm goes up whenever the button is
down and something is in reach — swinging at bedrock and having nothing happen is
correct feedback; not swinging reads as broken input.

#### Items are block types, and that will not last

A dropped item carries a `BlockId`. Breaking oak_log gives an oak_log, and everything
placeable is everything collectable.

**Crafting is what breaks this.** A stick is not a block and neither is a pickaxe, so
the item/block split has to happen then. It is deferred rather than pre-built because
an abstraction with one implementation is not an abstraction, and the shape the split
should take will be obvious once there is a recipe to satisfy. The cost of deferring
is a pass over `Inventory` and `ItemEntity` later; the cost of building it now is
carrying a distinction nothing uses.

Drops themselves are one field in `BlockTable` — a **name**, not a `BlockId`, because
`blockIdOf` cannot be called inside the table it searches. A `static_assert` proves
every name resolves, so the file keeps the property it exists for: a typo is a
compile error rather than a block that silently drops nothing.

#### A flat entity list, and the sweep that pays for it

Minecraft keeps entities in the chunk they occupy. That is right once they have to be
saved and ticked selectively, and it would mean threading entity lifetime through the
pin and unload machinery for a few dozen objects. A flat list plus an explicit "drop
anything whose column went away" sweep gets identical behaviour. The day this holds
thousands of entities is the day to move it, and not before.

**Physics is substepped, and a test is why.** The collision test asks whether the
destination is solid rather than sweeping the path to it, so any step longer than a
block tunnels straight through the floor — and a frame after a stall delivers exactly
that. The despawn test passed a 299-second `dt` and the item fell out of the world
instead of ageing, which is the same bug a window drag would have produced in play.
Ageing now runs on real elapsed time while physics is clamped to four steps of 1/60.

#### The HUD is not a UI framework

Screen-space quads in NDC, no projection matrix, one draw call: hotbar slots, block
icons, digits and the crosshair all go through one shader with a mode per quad. The
digit font is ten hard-coded 3x5 bit patterns expanded into a texture array at
startup, which keeps the rule that no binary asset ships with this repository.

There is deliberately **no cursor mode, no hit testing and no window**. Those are what
a slot-based inventory needs, and the count model exists precisely so they are not
needed yet — see `Inventory`, which is one `u32` per block type and nothing else.
That is enough for the whole loop that matters: break, watch it drop, pick it up,
place it, watch the number go down.

#### Measurements

Warm-up 3.52 s to **3.26 s**, which is noise rather than an improvement — nothing in
this change touches generation. Frame p99 **6.20 ms** against the 6.4-8.0 range
measured for the previous batch, so again within the spread this benchmark can
resolve and not evidence either way. Entities cost one pass over a list that is
usually empty, and the HUD is one draw call of about thirty quads.

185 tests, up from 176. asan clean.

---

### 7.11 Phase 12 — block updates, and the game tick underneath them

The last of the three missing subsystems. Its exit criterion is one sentence — "sand
falls when its support goes" — and the interesting part is that almost none of the
work is about sand.

#### The engine had no game tick, and that was the actual gap

Everything in this engine ran on frame delta time. That is right for anything a
camera watches move, and wrong for anything whose *rate* is part of the behaviour.
Minecraft notifies neighbours twenty times a second, and a cascade running at frame
rate would collapse a sand pillar three times faster on a 180 FPS machine than on a
60 FPS one — the same class of bug as the benchmark camera advancing by a fixed 1/60
step while frames ran at 5,000 FPS (7.7).

So `Engine::updateTicks` accumulates delta time and spends it in fixed 1/20 second
steps, capped at four per frame. The cap is the same trade `ItemEntities` makes for
its substeps: a two-second stall owes forty ticks, and spending them all in the frame
after it would be a second stall caused by the first. Dropping the backlog makes the
world lag real time by the length of the stall, which is invisible next to the stall.

**Flowing water is on this clock and will use it unchanged.** That is most of what
Phase 12 buys.

#### Notification is a queue, not recursion

`World::setBlock` already worked out which *meshes* an edit invalidates. Nothing
worked out which *blocks* it invalidates, which is why sand sat unsupported in
mid-air: no code anywhere asked it to look down.

An edit now queues its six face neighbours, and the next tick examines them. Six
rather than the twenty-six a mesher wants, because this is a support-and-adjacency
question rather than a shading one, and vanilla notifies the same six.

The queue matters more than it looks:

- **A collapsing pillar must not blow the stack**, and recursion through
  `setBlock → notify → setBlock` on a 384-block column would.
- **One block per tick is vanilla's look**, and it falls out of the queue rather than
  being arranged. A block that falls notifies the cell it left; the block above is
  examined on the *next* tick.
- **A refused edit comes back.** `setBlock` returns `Busy` when a meshing job holds
  the column, and every other writer in this engine retries rather than giving up.
  Here it is load-bearing rather than polite: giving up would leave sand hanging with
  nothing left to ask about it again.

Deduplication is a set, not a scan. A cascade queues hundreds of positions and each
of them notifies seven more, most of which are already queued; the quadratic version
is measurable and `BlockPosHash` is ten lines.

#### One behaviour, deliberately not a table of behaviours

`examine` decides one thing today: does this block fall. There is no virtual on a
block type and no dispatch table, for the same reason DESIGN.md 7.10 gives for not
pre-splitting items from blocks — an abstraction with one implementation is not an
abstraction. Flowing water is the second behaviour and the point at which the shape
of the dispatch becomes obvious. The queue above it does not change either way.

Which blocks fall is one `bool` in `world/BlockTable.hpp`, appended rather than
inserted so that the entries which spell themselves out positionally keep their
meaning. Sand and gravel, which is vanilla's whole list among the blocks this engine
has.

#### The safe read that will not stay safe

`examine` asks whether the block below is air. `World::blockAt` answers air for a
column that is not loaded or is still generating, so that reading looks like it could
mistake "I do not know" for "there is nothing holding this up" — and drop sand at the
edge of the loaded region into a column that has simply not arrived yet.

It cannot, and the reason is narrow: the block below is in the *same column*, so if
that column were not `Ready` the block being examined would have read as air too and
`isFalling(air)` is false. **Flowing water spreads sideways and this argument does not
survive it.** It is written down at the read rather than in a header, because that is
where the next person will be standing.

#### A falling block is an entity, and it cost one loop

Vanilla turns unsupported sand into a falling entity rather than stepping the block
down a cell at a time, and the difference is visible: stepping reads as a stutter,
falling reads as falling. `FallingBlocks` sits beside `ItemEntities` rather than
inside it — a dropped item spins, bobs, merges, despawns and can be picked up, and a
falling block does none of those and turns back into world geometry.

Its horizontal position is `i32`, because a falling block drops straight down the
column it left. There is no axis for float drift to live on.

**Rendering cost nothing.** `ItemRenderer`'s SSBO already carried the half-extent as
a per-entity field rather than as a constant, so a falling block is the same struct
with 0.5 in it and no spin: same shader, same buffer, same draw call. That was luck
rather than foresight, and it is why Phase 12 needed no fourth render path.

Physics runs on **frame** time while the scheduler runs on the tick. A cube
descending in 20 Hz steps against a 60 FPS camera visibly stutters, and interpolating
a position the simulation already knows exactly would be work to hide work.

Two failure modes are pinned by tests rather than by care:

- **A long delta must not drop a block through the floor.** The landing test asks
  which cell the bottom is in rather than sweeping to it, so this is the same bug a
  299-second `dt` found in `ItemEntities` (7.10). Substepping is the fix, and a
  `static_assert` that terminal velocity times the maximum substep stays under one
  block is what keeps it a fact rather than a hope.
- **A landing the world refuses must not delete the block.** The entity is the only
  copy that exists. A `Busy` landing holds position and retries; a landing into a cell
  something else has taken drops an item, which is vanilla's answer and needed no new
  machinery.

#### What the counters found, one session later

Worth adding here rather than only in the handoff, because it is the clearest
evidence any argument in this document has produced. The first session that ran with
the counters reported `broke 2 | placed 0 | collected 0 | 2 items` — and **item pickup
turned out never to have worked**. The radius was 1.4 measured from the eye; an item
rests 1.5 below it. Standing on an item was out of range. Fixed in 7.14.

7.10 shipped that and said the loop was closed. Three sessions played it and none
could tell, because nothing on the path logged anything. The counters cost four
integers and found it in 56 seconds. That is the argument for them, made better by
events than it was made in prose.

#### The counters that should have existed three sessions ago

Shipped first, before any of the above, because HANDOFF.md had been asking for them
since the second play session and asking again after the third. Blocks broken,
placed, collected, plus items and falling blocks alive and updates queued, on the
once-a-second stats line. Four integers, and every future session documents itself.

The gap they close is specific: three sessions in a row produced logs that could say
which *keys* were pressed and nothing about whether a single block was broken, because
the only pickup logging went in as `logDebug` and is off by default.

#### Known cost, not yet measured in play

`World::setBlock` recomputes a column's sky light, about 0.5 ms. That was documented
as a per-click cost; a falling block now spends it twice, once leaving and once
landing, and a six-block collapse is a dozen of them spread over as many ticks. In
open desert, where sand is at the surface and light genuinely moves, that is roughly
1 ms on one frame in three during a collapse. Acceptable, and the incremental relight
that World.hpp already names as the escape hatch is still the thing to reach for if it
appears in a profile.

#### Measurements

198 tests, up from 185, thirteen of them covering the queue, the cascade, the retry
discipline and both physics failures above. asan clean. tsan over the live pipeline
reports one race, entirely inside pulseaudio — GTK plays an event sound through
libcanberra, which starts a thread that races its own mutex allocation. Suppressed
with the rest of the desktop stack; no `mc::` frame appears as a racing access.

Warm-up **3.29–3.52 s** and frame p99 **4.40–5.86 ms** over three runs, with clean
sanity lines (1,089 of 1,089 columns loaded, none generating, none pinned).

Two things about that, and the second matters more than the first:

- **It is not evidence the feature is cheap, because the benchmark never triggers
  it.** The flight does not edit the world, so nothing is ever notified and nothing
  ever falls. What these numbers check is that an idle tick loop costs nothing, which
  is worth knowing and is not what the feature does. A sand collapse can only be
  measured by a person causing one.
- **The p99 came out *below* 7.10's 6.20 ms, and the honest explanation is that this
  is the first batch measured on a deliberately idle machine.** An earlier run in this
  same session, taken while an asan build occupied the other cores, reported a 4.02 s
  warm-up against the 3.29–3.52 s measured minutes later on the same binary. That is a
  ~20 % swing from background load alone — wider than several of the differences these
  documents have discussed and reasoned about. **No measurement in 7.5 through 7.10
  controlled for machine load**, so the older figures are not strictly comparable to
  these, and a difference of a millisecond between any two of them says less than it
  appears to. Nothing in Phase 12 should have made rendering faster, and the claim
  here is not that it did.

---

### 7.12 A real inventory, a UI layer, and hearts

Asked for immediately after Phase 12, and the request began with a complaint about
the HUD: nine blocks sat along the bottom of the screen and "vanilla does not do
that".

**Half of that was right and the half that was wrong mattered.** Vanilla does have a
hotbar and it does draw stack counts on it. What it does not do is give you slots for
things you do not own — and the old hotbar was `Engine::kHotbar`, a compile-time array
of nine block types, drawn at 32 % brightness when the player held none of them. In an
empty world that is nine grey blocks and nine zeroes for no reason. Acting on the
complaint as stated would have removed the hotbar and moved *away* from the game the
request was asking for; acting on what it was pointing at fixed it in one rule. **An
empty slot is empty.**

#### The container is the feature

The count model was chosen in 7.10 over slots, and the argument for it was that it
closes the break-drop-collect-place loop for a fraction of the work. That was correct
and it is why the loop works. What it got wrong is that carrying things is not felt as
a number going up: 36 slots of 64 is the thing, and one `u32` per block type is the
bookkeeping behind the thing. HANDOFF.md 1 has the full account, including that the
recommendation which lost was the one this project made.

Vanilla's shape, because the point is to feel like the game: 36 slots, the first nine
being the hotbar, and `add` filling partial stacks before empty ones and the hotbar
before the grid. Each of those is visible in play — the last one is why a block you
just mined is immediately placeable.

**Stack limits made a full inventory possible for the first time, and that turned out
to be a correctness change rather than a cosmetic one.** `ItemEntities::collect`
removed an item whether or not the caller had room for it, which was fine against an
unbounded count and silently deletes a stack against 36 slots. `collectInto` offers
each stack and takes back what did not fit, so a player with a full pack walks over a
diamond and it stays there.

#### One layout, or the pixels and the pointer drift apart

`InventoryLayout` computes every slot rectangle, and both the renderer and the hit
test go through it. That is the entire reason it is a class rather than two loops.
Two independent computations would agree until someone adjusted a margin, and then
clicks would land a few pixels off the icon they appear to be on — which reads as
*unresponsive* rather than as misaligned, and is miserable to find. A test walks every
slot at four aspect ratios, hit-tests its centre, and requires the answer to be the
slot it started from.

#### The UI layer is small on purpose

`HudRenderer`'s header used to say it was deliberately not a UI framework. It is one
now, and a deliberately poor one: no widget tree, no layout engine, no event routing.
There is one window, its geometry is a class, and a click is resolved by asking that
class which slot a point is in. **A second window is the moment to reconsider**, and a
chest or a crafting bench is exactly that — which is written in the header where the
next person will be standing.

Everything still goes through one shader, one buffer and one draw call, with a mode
per quad. Hearts reuse the glyph machinery the digits already had: a 7x7 bit pattern
expanded into the same texture array, with the half heart produced by masking the full
one to its left four columns rather than being drawn twice, so the two cannot drift
apart by a pixel. The empty heart is the full glyph in dark, for the same reason.

One deviation from vanilla, taken knowingly: **hearts stay visible while the inventory
is open.** Vanilla hides them because that screen shows the player model and armour in
their place; this window is slots and nothing else, so the space is free and health is
the only status the player has.

#### Hearts need damage, or they are decoration

Fall damage, because the engine already had gravity, a vertical velocity and a landing
test — the only new state is the height the player left the ground at. Vanilla's rule:
nothing for the first three blocks, then one half-heart per block, which is what makes
a jump free and a two-storey drop hurt.

Two details are load-bearing:

- **The fall is measured from where the ground was left, not from the peak.** A jump
  therefore pays for its own arc, which is why the three-block grace exists at all.
- **A column that has not streamed in does not count as a fall.** Walking already
  holds height rather than dropping through an unloaded column; without clearing the
  fall tracker there too, the game would damage the player for the streaming pipeline.

Death respawns with full health where the player stands. No death screen and no
dropped inventory: the first needs the UI layer to grow a second window and the second
needs somewhere for the items to go that the player can get back to. It logs rather
than pretending nothing happened.

#### Measurements

213 tests, up from 198. Fifteen new ones cover the slot rules, the click/split/swap
decisions, the full-inventory paths on both sides, and the layout property above.
asan clean.

`--inventory` seeds the player and opens the window at startup. It exists because
`--capture` cannot click, and the window is the only part of the engine that needs a
pointer to appear at all — the same reason `--fly` and `--first-person` exist. Both
states were checked by capture: an empty hotbar with ten hearts, and the open window
with stacks, two-digit counts, six full hearts, a half and three empty at health 13.

---

### 7.13 Water

RESEARCH.md 5.3 has said since it was written that water is three problems and not
one: aquifers inside the noise stage, water-against-water face culling in a mesher
that only knows `isOpaque`, and a translucent second draw pass. That was right, and
one of the three turned out to be a decision rather than a piece of work.

#### The aquifer is not built, and that is the scoping decision

Vanilla decides water, lava or air per 16x40x16 cell **inside the density
evaluation**, and one mechanism there produces oceans, rivers and flooded caves
alike. RESEARCH.md 6 records that the barrier noise, the fluid-level selection and
the interaction with carvers are not published anywhere usable — the entry has stood
unresolved since 4b, and closing it means working from decompiled `NoiseChunk` and
`Aquifer` code.

Set that aside and what remains is the part that is both well defined and almost all
of what a player sees: **the ocean**. Water fills each column from sea level (62)
down to the terrain surface, and stops.

Two consequences, both deliberate and both written at the code:

- **Caves under the sea stay dry.** Filling down to the first solid instead would
  pour water down every carved shaft and hang 1x1 columns of it in the middle of
  caverns. Flooded caves are the aquifer feature; this is the line where that
  feature begins.
- **A column whose terrain reaches above sea level gets no water at all**, even at
  its base. Deciding otherwise needs to know whether a space under an overhang is
  open to the sea sideways, which a per-column scan cannot know. The error is a dry
  pocket at the foot of an overhang; the alternative error is water hanging in a
  cliff face, which is much the worse of the two to look at.

The surface rules needed no change: `kBeachLevel` already put sand rather than grass
below 66, and trees already refuse to plant on anything but grass, so nothing grows
out of the sea bed.

**One thing about the ordering did bite, and a test found it rather than a
screenshot.** The flood's floor is `terrainTop`, which is the *density field's*
surface — and the thin-cave carver runs after that field is sampled. Where a cave
breaks through the sea bed, that voxel is air by the time the flood runs, so the
lowest water block came to rest on a hole: water hanging in mid-air over a cave
mouth. Thirteen of them in three columns.

The column is now skipped when its sea bed has been carved open, rather than filled
deeper. Following the hole down to the first real solid would drop a one-block-wide
pillar of still water through a cavern, which looks worse than the small dry patch
this leaves — and is the flooded-cave behaviour that belongs to the aquifer system
above. **The general shape of this mistake is worth remembering: a value computed in
one stage of an ordered pipeline describes the world as of that stage, not as of the
stage using it.**

#### `opaque` was answering two questions

Adding one non-solid block type exposed that `!= kAirBlock` had been standing in for
"is this solid" everywhere, and the two had been indistinguishable because every
block was both. Water separates them, and the list of places that had to change is
the interesting part:

- walking's ground probe — otherwise the player stands **on** the sea;
- item physics — otherwise dropped blocks float;
- falling-block support and landing — sand should fall *through* water;
- the aim ray — a crosshair over the ocean should find the sea bed, since there is
  no bucket and nothing can be done with the surface.

`BlockInfo` therefore carries `fluid` next to `opaque`, and `isSolidBlock` is the
test almost every caller of `blockAt` actually wanted. Glass, when it arrives, will
want `opaque` false and `fluid` false — which is the shape that says these are two
questions rather than one with a bad name.

#### The mesher needed the cull mask to stop being the occupancy mask

Binary greedy meshing finds faces with `col & ~(col >> 1)`: a voxel is occupied and
its neighbour is not. That expression uses one mask for both halves, which is exactly
right while the only thing that hides a face is the same thing that emits one.

Water breaks that. Water emits a face; water **and** rock hide one. So the mesher
now builds two column sets — `col*` for what emits and `cull*` for what hides — and
the shift uses the second. On the opaque pass they are identical and the generated
code is unchanged; on the fluid pass the cull set is water plus rock, which is what
makes the inside of an ocean produce nothing at all.

The neighbour shell decodes fluid occupancy too, and that matters more than it
sounds: without it, every 32-block section boundary inside a sea would emit a wall of
quads, drawing a visible grid through the water. A test pins it — a section of ocean
surrounded by ocean meshes to zero quads.

The second pass is skipped whole when the centre section holds no fluid, which is
almost every section in the world: everything above sea level and everything below
the sea bed. The cost of water existing is one boolean for them.

#### One arena range, two draws

`ChunkMesh` is opaque quads followed by translucent ones with a split index, rather
than two lists. Two lists would mean two allocations, two retirements and two
lifetimes to keep in step, for what is arithmetic on one offset. `Placement` carries
the split, and the renderer issues two multi-draws over the same range.

`gl_DrawID` counts from zero within each multi-draw, so the second pass needs its
section origins found at an offset — one `u_drawIdBase` uniform, zero for the first
pass, which is what it effectively always was.

Three pieces of state, and each is a specific failure if left out:

- **Depth writes off.** A translucent surface that writes depth hides what is behind
  it, including the rest of the water, and an ocean renders as its nearest section
  and nothing else. Depth *testing* stays on, so the sea bed and any terrain in front
  still occlude correctly.
- **Back-face culling off.** The mesher emits only the outside of a body of water, so
  a lake's surface is a single sheet whose one face points up — and with culling on
  it vanishes the moment the camera goes under it. A swimmer would look up at open
  sky. Vanilla draws water double-sided for the same reason.
- **Alpha from the texture rather than 1.** Every opaque layer stores 255, so this
  changes nothing for them; the water layer stores ~0.75.

**There is no back-to-front sort, and that is a real limitation rather than an
oversight.** Correct blending of overlapping translucent surfaces needs one. Water
gets away without it because it is the only translucent thing in the world and very
nearly a single flat sheet: two water surfaces are rarely both in front of the same
pixel. Looking across a bay at a second body of water is where it shows, and a second
translucent block type is where it stops being defensible.

#### Swimming, such as it is

Water holds nothing up, so without something the player sinks to the sea bed at full
gravity and walks around down there. Buoyancy is a quarter of gravity with a low sink
speed, and `Space` paddles up. Entering water clears the fall tracker, which is
vanilla's rule and the reason jumping off a cliff into a lake is a thing people do.

No air meter, no drowning, no swimming pose. Those are a survival system rather than
a fluid, and none of them is what "the world has oceans" was asking for.

#### Measurements

221 tests, up from 213. Eight new ones, and the ones worth naming are the mesher's:
a slab of water meshes to six quads rather than to its 8,192 internal faces, an
ocean section surrounded by ocean meshes to none, the opaque/translucent split is a
real partition rather than a hint, and the culled mesher — the oracle — covers the
same water area the greedy one does.

Warm-up **3.36–3.47 s** and frame p99 **4.88–5.18 ms** over three runs on an idle
machine, against 3.29–3.52 s and 4.40–5.86 ms measured the same way immediately
before water. **Water is not distinguishable from noise in either.** Arena use is
unchanged at 115 MiB, and a capture from the spawn point draws 4.343 M quads against
4.332 M without water — **0.27 % more geometry for every ocean in the render
distance.**

That last number is the one worth understanding rather than just recording. Water is
nearly free to draw for the same reason the fully-enclosed saving disappeared when
caves landed, run backwards: an ocean is a flat sheet, greedy meshing merges a flat
sheet into very few quads, and the second pass is skipped entirely for every section
that holds no fluid — which is everything above sea level and everything below the
sea bed. The expensive translucent surface would be one that is *not* flat.

### 7.14 Item pickup, which had never worked

The stats counters added in 7.11 found this in their first session, and it is the
best argument for them that could have been constructed: **two blocks broken, two
items still lying in the world at exit, zero collected.**

#### The arithmetic

Pickup was a 1.4-block sphere measured from `m_camera.position()`, which is the
**eye**. A dropped item comes to rest at ground + `kHalfSize` (0.12); the eye sits at
ground + `kEyeHeight` (1.62). Standing directly on top of an item is therefore 1.50
away from it against a radius of 1.4 — out of range while standing on it, on flat
ground, always.

It is not *literally* never: an item resting on a ledge one block above the player's
feet is about 0.71 away and was collectible. That is the only case that ever worked,
and it is not the case anybody plays.

**This shipped in 7.10 under the claim that the loop was closed** — "break a block,
watch it drop, walk over it, see the count go up". The break, the drop and the count
were all real. The walk-over never was.

#### Why nothing caught it

`ItemEntities` was well tested. Six cases covered gravity, merging, despawn, column
unload, falling out of the world and a full inventory refusing a stack — and every
one of them called `collect(position, radius)` with a position and a radius **of its
own choosing**, typically the item's own height and a radius of 2 or 3.

So the tested unit was correct and the bug was in neither of its two inputs
separately. It was in the *relationship* between `Engine::kPickupRadius` and
`CharacterRenderer::kEyeHeight` — two constants in two different modules, combined at
one call site, with nothing asserting anything about the pair. A test suite can have
full coverage of a class and zero coverage of the only thing that was wrong.

#### The fix is the reference point, not the radius

Enlarging the radius to 1.6 would have made the symptom go away and left the shape of
the error in place, ready to come back the moment the eye height or the item's rest
height moved. Vanilla measures from the player's **bounding box**, which is precisely
why it does not care where the eye is.

`ItemEntities::PickupVolume` is that answer without a collider: a vertical segment
from the feet to the top of the head, plus a radius around it. `distanceSquaredTo`
clamps onto the segment before measuring, so an item anywhere between the feet and
the head is at zero vertical distance and only the horizontal reach matters.
`placeTargetBlock` already used this shape of test for "am I standing here", and both
would still be better served by the real collider section 8 wants.

Three things moved to make the relationship testable, and that was most of the work:

- **`kPickupRadius` moved from `Engine` to `ItemEntities`.** It was a private
  constant in the caller, so no test could reach it.
- **`collect` and `collectInto` take a `PickupVolume`, not a point.** The point form
  is deleted rather than kept as an overload — it is the bug, and every caller now
  has to say where the player's *body* is.
- **`Engine::playerFeet()` exists.** The subtraction was spelled out at four call
  sites and the fifth caller passed the eye instead. A named accessor is how the next
  one gets it right by construction.

`tests/test_items.cpp` now builds its volumes from `CharacterRenderer::kHeight` and
`ItemEntities::kPickupRadius` — the engine's real numbers, not chosen ones — and one
case reaches for the same item from the eye and requires it to *fail*, so a radius
enlarged back into the old failure is a red test rather than a silent regression.
That is the one place a world-layer test includes a render header, and
`tests/CMakeLists.txt` says why.

**224 tests pass, asan and tsan clean.** And it is **confirmed in play**: the fifth
session (111 seconds, fullscreen, locked 60 FPS) reported `broke 5 | collected 5 |
0 items`, each collect landing on the stats line after the break that caused it.
The fourth session's `broke 5 | collected 2` was three items never walked over, not
three that failed.

#### What this says about the project's testing, and it is not "write more tests"

The counters were the thing that worked. Three sessions of play could not find this
because nothing on the path logged; one session with four integers found it
immediately. **The lesson is that a subsystem's tests do not cover the seam where two
subsystems meet, and the cheapest instrument at that seam is a counter, not a test.**

---

## 8. Open Questions

Filled in with recommended defaults above, but expected to need revisiting once
implementation makes contact with reality:

- ~~**Actual benefit of AO-aware merging**~~ — **resolved in Phase 2** (see
  7.3). AO-aware merging costs 13.5 percentage points of reduction, not the
  feared collapse to ~15%. Kept for near chunks; still disabled for LOD levels.
  Re-measure once caves and overhangs exist (Phase 4).
- ~~**Block texture source**~~ — **resolved in Phase 2**: generated
  procedurally, no binary assets in the repository.
- **Occlusion culling method** — HZB, visibility graph, or both. Decided by
  profiling in Phase 8.
- ~~**World persistence** — whether it is in scope at all~~ — **in scope as of the
  2026-08-11 scope change**, as Phase 11. Once a player can edit the world, throwing
  the edits away on exit is a defect rather than a simplification. The disk format is
  still undecided; palette-compressed sections are already compact, so the open part
  is the container and whether it compresses at all.

---

## 9. References

- [cgerikj/binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing) — reference implementation and timings
- [Auburn/FastNoise2](https://github.com/Auburn/FastNoise2) — node-graph SIMD noise
- [omar-owis/VoxelEngine](https://github.com/omar-owis/VoxelEngine) — GPU-driven C++23 / OpenGL 4.6 voxel engine
- [Minecraft Wiki — Noise settings / density functions](https://minecraft.wiki/w/Noise_settings) — terrain generation model
- [xCollateral/VulkanMod](https://github.com/xCollateral/vulkanmod) — chunk culling and indirect draw techniques
- [NVIDIA Research — Efficient Sparse Voxel Octrees](https://research.nvidia.com/publication/efficient-sparse-voxel-octrees) — background for the brickmap/SVO far field
- [Nick's Blog — Homebrew Voxel Engine](https://nickmcd.me/2019/10/27/homebrew-voxel-engine/)
