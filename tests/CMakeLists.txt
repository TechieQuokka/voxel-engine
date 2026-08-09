# Tests link individual module libraries, never the whole executable. That keeps
# an mc_world test from silently acquiring a dependency on mc_render.

add_executable(mc_tests
  test_main.cpp
  test_result.cpp
  test_bitpack.cpp
  test_coords.cpp
  test_palette.cpp
  test_mesher.cpp
  test_greedy_mesher.cpp
)

target_link_libraries(mc_tests PRIVATE
  mc_core
  mc_world
  mc_mesh
  doctest::doctest
)

mc_set_warnings(mc_tests)

add_test(NAME mc_tests COMMAND mc_tests)
