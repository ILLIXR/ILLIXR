# OpenCV

# Intel RealSense
find_package(realsense2 REQUIRED)
include_directories("${realsense2_INCLUDE_DIR}")
add_library(plugin SHARED plugin.cpp)

if(NOT HAVE_CENTOS)
    find_package(OpenCV REQUIRED)
else()
    add_dependencies(plugin OpenCV_Viz)
endif()
include_directories(${OpenCV_INCLUDE_DIRS})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
target_link_libraries(plugin ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY})

install(TARGETS plugin DESTINATION lib)
