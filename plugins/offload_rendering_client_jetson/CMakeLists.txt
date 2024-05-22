cmake_minimum_required(VERSION 3.16)

project(offload_rendering_client)

set(PLUGIN_NAME plugin.offload_rendering_client${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external_for_plugin(Vulkan)
if(USE_MONADO)
    get_external_for_plugin(Monado_vk)
endif()

include_directories(/usr/src/jetson_multimedia_api/include)
include_directories(/usr/src/jetson_multimedia_api/include/libjpeg-8b)

# add *.cpp from /usr/src/jetson_multimedia_api/samples/common/classes
file(GLOB_RECURSE JETSON_MULTIMEDIA_API_CLASSES /usr/src/jetson_multimedia_api/samples/common/classes/*.cpp)

# remove NvVulkanRenderer.cpp from the list
list(REMOVE_ITEM JETSON_MULTIMEDIA_API_CLASSES /usr/src/jetson_multimedia_api/samples/common/classes/NvVulkanRenderer.cpp)

link_directories(/usr/lib/aarch64-linux-gnu/tegra)

add_executable(decode_cli decoding/video_decode.cpp decoding/video_decode_csvparser.cpp ${JETSON_MULTIMEDIA_API_CLASSES}
        decoding/NvVulkanRenderer.cpp)

find_package (Threads)
target_link_libraries(decode_cli ${CMAKE_THREAD_LIBS_INIT})

# drm
target_link_libraries(decode_cli drm)
target_link_libraries(decode_cli nvjpeg)
target_link_libraries(decode_cli nvv4l2)
target_link_libraries(decode_cli EGL)
target_link_libraries(decode_cli GLESv2)
target_link_libraries(decode_cli X11)
target_link_libraries(decode_cli nvbufsurface)
target_link_libraries(decode_cli nvbufsurftransform)
target_link_libraries(decode_cli nvosd)

# link libdrm
target_link_libraries(decode_cli ${Vulkan_LIBRARIES})
target_include_directories(decode_cli BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} /usr/include/libdrm)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/vk/display_provider.hpp
        ../../include/illixr/vk/vulkan_utils.hpp
        ../../include/illixr/serializable_data.hpp
        ../../include/illixr/serializable_data.cpp
        ${JETSON_MULTIMEDIA_API_CLASSES}
        decoding/video_decode.h
        decoding/video_decode.cpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()

target_link_libraries(${PLUGIN_NAME} spdlog::spdlog ${Vulkan_LIBRARIES})
target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)



install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
