# Tests link individual module libraries, never the whole executable. That keeps
# an mc_core test from silently acquiring a dependency on mc_rhi.

add_executable(mc_tests
  test_main.cpp
  test_result.cpp
)

target_link_libraries(mc_tests PRIVATE
  mc_core
  doctest::doctest
)

mc_set_warnings(mc_tests)

add_test(NAME mc_tests COMMAND mc_tests)
