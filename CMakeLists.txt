cmake_minimum_required(VERSION 3.25)

project(minecraft
  VERSION 0.0.1
  DESCRIPTION "High-performance voxel engine"
  LANGUAGES C CXX   # C is required by the GLFW and glad sources
)

# ---------------------------------------------------------------------------
# Language settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # -std=c++20, not -std=gnu++20

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This project targets Linux only.")
endif()

option(MC_ENABLE_TRACY "Build with Tracy profiler instrumentation" OFF)
option(MC_BUILD_TESTS  "Build unit tests"                          ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)
include(CompilerWarnings)

# ---------------------------------------------------------------------------
# Dependencies
#
# Every dependency is pinned to an explicit tag, never a branch. A silent
# upstream change can shift this engine's performance characteristics, and that
# kind of change has to be deliberate.
#
# SYSTEM YES marks third-party includes as system headers so our -Werror
# settings do not fail on their code.
# ---------------------------------------------------------------------------
CPMAddPackage(
  NAME glm
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG 1.0.1
  SYSTEM YES
  OPTIONS
    "GLM_BUILD_TESTS OFF"
    "BUILD_SHARED_LIBS OFF"
)

CPMAddPackage(
  NAME glfw
  GITHUB_REPOSITORY glfw/glfw
  GIT_TAG 3.4
  SYSTEM YES
  OPTIONS
    "GLFW_BUILD_DOCS OFF"
    "GLFW_BUILD_TESTS OFF"
    "GLFW_BUILD_EXAMPLES OFF"
    "GLFW_INSTALL OFF"
)

# glad2 generates its loader at configure time; it needs python3 + jinja2.
CPMAddPackage(
  NAME glad
  GITHUB_REPOSITORY Dav1dde/glad
  GIT_TAG v2.0.8
  DOWNLOAD_ONLY YES
)
if(glad_ADDED)
  add_subdirectory("${glad_SOURCE_DIR}/cmake" glad_cmake)
  glad_add_library(glad_gl_core_46 REPRODUCIBLE API gl:core=4.6)
endif()

if(MC_ENABLE_TRACY)
  CPMAddPackage(
    NAME tracy
    GITHUB_REPOSITORY wolfpld/tracy
    GIT_TAG v0.11.1
    SYSTEM YES
    OPTIONS "TRACY_ENABLE ON"
  )
endif()

if(MC_BUILD_TESTS)
  CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.4.11
    SYSTEM YES
  )
  enable_testing()
endif()

# ---------------------------------------------------------------------------
# Modules
#
# One static library per module. Dependency direction is declared with
# target_link_libraries and therefore enforced at link time rather than merely
# documented. See docs/DESIGN.md section 5.
#
# Modules not listed here (world, worldgen, mesh, render) are introduced in
# their respective phases.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/rhi)
add_subdirectory(src/app)

if(MC_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Assets
#
# Symlinked rather than copied so shader edits take effect without a rebuild.
# Paths are resolved at runtime relative to the executable, never the working
# directory (see core/Paths.hpp).
# ---------------------------------------------------------------------------
add_custom_target(mc_assets ALL
  COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:minecraft>/assets"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          "${CMAKE_CURRENT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:minecraft>/assets"
  COMMENT "Linking assets/ next to the executable"
)
add_dependencies(minecraft mc_assets)
