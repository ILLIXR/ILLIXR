cmake_minimum_required(VERSION 3.17)

project(native_renderer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan COMPONENTS glslc)

add_library(native_renderer SHARED plugin.cpp)
set_target_properties(native_renderer PROPERTIES OUTPUT_NAME "plugin")

target_link_libraries(native_renderer ${Vulkan_LIBRARIES})
target_include_directories(native_renderer PUBLIC ${Vulkan_INCLUDE_DIRS})
