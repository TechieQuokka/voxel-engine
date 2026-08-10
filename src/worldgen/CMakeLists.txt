add_library(mc_worldgen STATIC
  Generator.cpp
)

# worldgen knows world, and nothing above it. Note the absence of mc_render and
# mc_mesh: how terrain is produced and how it is drawn are separate concerns, and
# the link line is what keeps them that way.
#
# FastNoise2 becomes a PRIVATE dependency here in Phase 4.
target_link_libraries(mc_worldgen PUBLIC mc_core mc_world)

mc_set_warnings(mc_worldgen)
