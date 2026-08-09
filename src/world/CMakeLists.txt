add_library(mc_world STATIC
  BlockRegistry.cpp
  Palette.cpp
)

# world is pure data: it knows nothing about rendering, so that the same
# storage feeds both the raster path and the Phase 7 ray marcher.
target_link_libraries(mc_world PUBLIC mc_core)

mc_set_warnings(mc_world)
