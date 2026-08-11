add_library(mc_render STATIC
  BlockTextures.cpp
  Camera.cpp
  CharacterRenderer.cpp
  ChunkRenderer.cpp
  Frustum.cpp
  SectionMeshStore.cpp
  SelectionRenderer.cpp
)

# Note the absence of mc_worldgen: the renderer has no business knowing how
# terrain was produced. Including a worldgen header here fails at link time.
target_link_libraries(mc_render PUBLIC
  mc_core
  mc_rhi
  mc_world
  mc_mesh
)

mc_set_warnings(mc_render)
