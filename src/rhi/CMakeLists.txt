add_library(mc_rhi STATIC
  Device.cpp
  Shader.cpp
  VertexArray.cpp
)

target_link_libraries(mc_rhi
  PUBLIC  mc_core
  PRIVATE glad_gl_core_46   # PRIVATE: no GL type appears in rhi's headers.
                            # This is the seam a Vulkan backend would target.
)

mc_set_warnings(mc_rhi)
