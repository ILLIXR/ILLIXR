# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Intel RealSense
find_package(realsense2 REQUIRED)
include_directories("${realsense2_INCLUDE_DIR}")

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY})

