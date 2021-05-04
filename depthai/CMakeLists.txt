cmake_minimum_required(VERSION 3.2)

set(BUILD_SHARED_LIBS ON)
# Add depthai-core dependency
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/depthai-core EXCLUDE_FROM_ALL)

set(TARGET_NAME myapp)
project(${TARGET_NAME})

# OpenCV
find_package(OpenCV REQUIRED )
include_directories(${OpenCV_INCLUDE_DIRS})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3 -fPIC)
add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin ${DEPENDENCIES} ${OpenCV_LIBRARIES} depthai-core)