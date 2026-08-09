add_library(mc_platform STATIC
  Window.cpp
)

target_link_libraries(mc_platform
  PUBLIC  mc_core
  PRIVATE glfw    # PRIVATE: no GLFW type appears in platform's headers
)

mc_set_warnings(mc_platform)
