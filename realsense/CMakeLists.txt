cmake_minimum_required(VERSION 3.1.0)

project(RealSenseImShowExample)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Intel RealSense
find_package(realsense2 REQUIRED)
if (realsense2_FOUND)
    include_directories("${realsense2_INCLUDE_DIR}")
endif (realsense2_FOUND)

add_definitions(-Wno-format-extra-args -std=c++17 -g -O3)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY})

