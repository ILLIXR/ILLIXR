cmake_minimum_required(VERSION 3.16)

project(native_renderer)

set(PLUGIN_NAME plugin.native_renderer${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan COMPONENTS glslc)
# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/global_module_defs.hpp
        ../../include/illixr/pose_prediction.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/vk_util/display_sink.hpp
        ../../include/illixr/vk_util/render_pass.hpp
        ../../include/illixr/gl_util/lib/tiny_obj_loader.h
        )

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME "plugin")

target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES} ${OpenCV_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
