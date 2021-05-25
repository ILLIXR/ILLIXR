cmake_minimum_required(VERSION 3.2)
project(depthai_plugin)

# Add depthai-core dependency
find_package(depthai CONFIG REQUIRED)

# OpenCV
find_package(OpenCV REQUIRED )
include_directories(${OpenCV_INCLUDE_DIRS} ${DepthAI_INCLUDE_DIRS})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE depthai::core)