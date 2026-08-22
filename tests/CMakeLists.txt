# Tests link individual module libraries, never the whole executable. That keeps
# an mc_world test from silently acquiring a dependency on mc_render.
#
# mc_render is on the list for Camera and Frustum, which are pure math. Nothing in
# the test binary creates a GL context, so no test may reach into rhi -- the link
# succeeds because glad's symbols come along, not because a context exists.
#
# test_items.cpp includes render/CharacterRenderer.hpp, which is allowed for the same
# reason: a header include is not reaching into rhi, though constructing anything in
# it would be.
#
# **The player's height and eye height no longer live there.** They were in the
# renderer, and item pickup shipped broken because nothing could test `kPickupRadius`
# against them; block placement then grew a collision box from the same numbers and
# got a height with no width. Both are in `world/PlayerBox.hpp` now, which is why
# test_player_box.cpp needs nothing but mc_world.

add_executable(mc_tests
  test_main.cpp
  test_result.cpp
  test_bitpack.cpp
  test_mpmc_queue.cpp
  test_jobsystem.cpp
  test_range_allocator.cpp
  test_section_mesh_arena.cpp
  test_ring_layout.cpp
  test_frustum.cpp
  test_coords.cpp
  test_palette.cpp
  test_chunk.cpp
  test_chunk_codec.cpp
  test_persistence.cpp
  test_world.cpp
  test_density_field.cpp
  test_generator.cpp
  test_mesher.cpp
  test_skylight.cpp
  test_block_light.cpp
  test_greedy_mesher.cpp
  test_raycast.cpp
  test_world_edit.cpp
  test_block_table.cpp
  test_trees.cpp
  test_items.cpp
  test_block_updates.cpp
  test_inventory.cpp
  test_crafting.cpp
  test_water.cpp
  test_flowing_water.cpp
  test_player_box.cpp
  test_fall_damage.cpp
  test_player.cpp
  test_player_codec.cpp
  test_walk_move.cpp
  test_item_model.cpp
  test_furnace.cpp
  test_progression.cpp
)

target_link_libraries(mc_tests PRIVATE
  mc_core
  mc_world
  mc_worldgen
  mc_mesh
  mc_render
  doctest::doctest
)

mc_set_warnings(mc_tests)

# One ctest entry per doctest case, rather than one entry for the whole binary.
#
# The binary as a single test made `ctest -j` do nothing and reported a failure as
# "mc_tests failed" with the name of the case buried in the output. Discovery runs
# the binary once at build time with --list-test-cases and registers what it finds,
# so a case added to a .cpp file needs no change here.
#
# **The cases that write files are the reason this matters and the reason it needs
# care.** Under -j they run as separate processes, which is what makes
# test_persistence's RLIMIT_FSIZE case safe to have: its process-wide limit can no
# longer reach a case running beside it. TempDir already keys its directory off the
# pid, so parallel cases do not collide.
#
# **Except under ThreadSanitizer.** Discovery runs the binary at build time to ask
# it for its case names, and tsan will not start on this kernel without ASLR
# disabled -- so the listing fails and takes the whole build down with it. doctest
# 2.4.11 offers only POST_BUILD discovery, with no PRE_TEST mode to defer it, and
# wrapping the discovery command in `setarch` would bake a Linux-only workaround
# into every build. tsan is driven by hand through `setarch -R` anyway (HANDOFF
# section 2), where one entry is all it ever needed.
if(CMAKE_CXX_FLAGS MATCHES "sanitize=thread")
  add_test(NAME mc_tests COMMAND mc_tests)
else()
  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  doctest_discover_tests(mc_tests)
endif()
