add_library(mc_mesh STATIC
  CulledMesher.cpp
)

target_link_libraries(mc_mesh PUBLIC mc_core mc_world)

mc_set_warnings(mc_mesh)
