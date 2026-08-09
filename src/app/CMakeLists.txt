add_executable(minecraft
  main.cpp
  Engine.cpp
)

target_link_libraries(minecraft PRIVATE
  mc_core
  mc_platform
  mc_rhi
)

mc_set_warnings(minecraft)
