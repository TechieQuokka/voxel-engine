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
  test_ring_layout.cpp
  test_frustum.cpp
  test_coords.cpp
  test_palette.cpp
  test_chunk.cpp
  test_world.cpp
  test_density_field.cpp
  test_generator.cpp
  test_mesher.cpp
  test_skylight.cpp
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

add_test(NAME mc_tests COMMAND mc_tests)
