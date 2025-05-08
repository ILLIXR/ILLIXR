cmake_minimum_required(VERSION 3.16)

project(offload_rendering_client)

set(PLUGIN_NAME plugin.offload_rendering_client${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ffmpeg
get_external_for_plugin(ILLIXR_FFmpeg)

find_package(CUDAToolkit REQUIRED)

add_compile_definitions(ILLIXR_LIBAV)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            offload_rendering_client.hpp
            offload_rendering_client.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serializable_data.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/ffmpeg_utils.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
            ${CMAKE_SOURCE_DIR}/utils/serializable_data.cpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()
if(ILLIXR_FFmpeg_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${ILLIXR_FFmpeg_DEP_STR})
endif()

target_link_libraries(${PLUGIN_NAME} illixr_vulkan_utils spdlog::spdlog ${Vulkan_LIBRARIES})
target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} ${libavdevice_illixr_LIBRARIES} ${libavformat_illixr_LIBRARIES} ${libavcodec_illixr_LIBRARIES} ${libavutil_illixr_LIBRARIES} ${libswscale_illixr_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${libavcodec_illixr_INCLUDE_DIRS})

target_link_libraries(${PLUGIN_NAME} CUDA::cudart CUDA::nppif CUDA::nppial CUDA::nppicc CUDA::nppidei CUDA::nppig CUDA::nppim CUDA::nppist CUDA::nppisu CUDA::nppitc CUDA::npps CUDA::nvToolsExt)
target_include_directories(${PLUGIN_NAME} PUBLIC ${CUDAToolkit_INCLUDE_DIRS})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
