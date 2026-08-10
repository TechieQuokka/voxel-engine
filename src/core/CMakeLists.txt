add_library(mc_core STATIC
  Assert.cpp
  JobSystem.cpp
  Log.cpp
  Paths.cpp
)

# src/ is on the include path so headers are included as "core/Log.hpp",
# making the owning module visible at every use site.
target_include_directories(mc_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

# Threads is PUBLIC: JobSystem.hpp exposes std::jthread and std::counting_semaphore,
# so every consumer needs the pthread link too.
find_package(Threads REQUIRED)
target_link_libraries(mc_core PUBLIC glm::glm Threads::Threads)

if(MC_ENABLE_TRACY)
  target_link_libraries(mc_core PUBLIC TracyClient)
  target_compile_definitions(mc_core PUBLIC MC_TRACY_ENABLED)
endif()

mc_set_warnings(mc_core)
