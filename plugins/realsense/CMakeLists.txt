# CMake file for building realsense plugin for ILLIXR
# Required packages:
#   realsense (development)
#   OpenCV including the viz module  (development)

# Intel RealSense
find_package(realsense2 REQUIRED)
include_directories("${realsense2_INCLUDE_DIR}")
add_library(plugin.realsense${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.realsense${ILLIXR_BUILD_SUFFIX} OPENCV_LIBRARY)
endif()
include_directories(${OpenCV_INCLUDE_DIRS})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
target_link_libraries(plugin.realsense${ILLIXR_BUILD_SUFFIX} ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY})

install(TARGETS plugin.realsense${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
