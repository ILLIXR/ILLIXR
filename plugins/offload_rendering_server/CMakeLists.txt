cmake_minimum_required(VERSION 3.16)

project(offload_rendering_server)

set(PLUGIN_NAME plugin.offload_rendering_server${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external_for_plugin(Vulkan)
if(USE_MONADO)
    get_external_for_plugin(Monado_vk)
endif()

# ffmpeg
get_external_for_plugin(ILLIXR_FFmpeg)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/vk/display_provider.hpp
        ../../include/illixr/vk/vulkan_utils.hpp
        ../../include/illixr/vk/ffmpeg_utils.hpp
        ../../include/illixr/serializable_data.hpp
        ../../include/illixr/serializable_data.cpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()
if(ILLIXR_FFmpeg_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${ILLIXR_FFmpeg_DEP_STR})
endif()

target_link_libraries(${PLUGIN_NAME} spdlog::spdlog ${Vulkan_LIBRARIES})
target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} ${libavdevice_illixr_LIBRARIES} ${libavformat_illixr_LIBRARIES} ${libavcodec_illixr_LIBRARIES} ${libavutil_illixr_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${libavcodec_illixr_INCLUDE_DIRS})
message("____________ ${libavcodec_illixr_INCLUDE_DIRS}")
get_target_property(INC_DIRS ${PLUGIN_NAME} INCLUDE_DIRECTORIES)
message("____________ ${INC_DIRS}")
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
