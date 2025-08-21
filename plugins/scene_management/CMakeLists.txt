cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)

set(PLUGIN_NAME plugin.scene_management${ILLIXR_BUILD_SUFFIX})

# Find OpenMP
find_package(OpenMP REQUIRED)

# Find Draco library
find_package(draco_illixr REQUIRED CONFIG)

# Add the plugin library
add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            spatial_hash.cpp
            spatial_hash.hpp
)

# Link OpenMP
target_link_libraries(${PLUGIN_NAME} OpenMP::OpenMP_CXX draco_illixr::draco ${OpenCV_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
