add_library(mc_worldgen STATIC
  DensityField.cpp
  DensityGraph.cpp
  Features.cpp
  Generator.cpp
  TerrainProbe.cpp
)

# worldgen knows world, and nothing above it. Note the absence of mc_render and
# mc_mesh: how terrain is produced and how it is drawn are separate concerns, and
# the link line is what keeps them that way.
#
# FastNoise2 is PRIVATE: no FastNoise type appears in any header here, so it can be
# replaced -- or moved to the GPU -- by changing DensityGraph.cpp alone. Same rule
# glad follows in rhi and GLFW in platform.
target_link_libraries(mc_worldgen
  PUBLIC  mc_core mc_world
  PRIVATE FastNoise
)

mc_set_warnings(mc_worldgen)
