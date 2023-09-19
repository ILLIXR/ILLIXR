cmake_minimum_required(VERSION 3.16)

project(offload_rendering_client)

set(PLUGIN_NAME plugin.offload_rendering_client${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
if(USE_MONADO)
    get_external_for_plugin(Monado_vk)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/vk/display_sink.hpp
        ../../include/illixr/vk/vulkan_utils.hpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} glfw)
target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
