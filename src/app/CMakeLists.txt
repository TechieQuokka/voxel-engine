add_executable(minecraft
  main.cpp
  Engine.cpp
  ChunkStreamer.cpp
  CaptureScenarios.cpp
)

target_link_libraries(minecraft PRIVATE
  mc_core
  mc_platform
  mc_rhi
  mc_world
  mc_worldgen
  mc_mesh
  mc_render
)

mc_set_warnings(minecraft)
