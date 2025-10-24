cmake_minimum_required(VERSION 3.16)

project(offload_rendering_server)

# Note that the offload rendering server only supports Monado (not native)
get_external_for_plugin(Monado_vk)
set(PLUGIN_NAME "plugin.offload_rendering_server${ILLIXR_BUILD_SUFFIX}")
set(PLUGIN_SOURCE "${CMAKE_BINARY_DIR}/plugins/offload_rendering_server/plugin.cpp")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CUDA REQUIRED)

# NVENC SDK headers
if(NOT DEFINED ENV{VIDEO_CODEC_SDK_ROOT})
  message(FATAL_ERROR "Set VIDEO_CODEC_SDK_ROOT to your Video_Codec_SDK directory")
endif()
set(VCS $ENV{VIDEO_CODEC_SDK_ROOT})

# ffmpeg
get_external_for_plugin(ILLIXR_FFmpeg)

add_compile_definitions(ILLIXR_LIBAV)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            offload_rendering_server.cpp
            offload_rendering_server.hpp
            NvencEncoder.hpp
            NvencEncoder.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serializable_data.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/ffmpeg_utils.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
            ${CMAKE_SOURCE_DIR}/utils/serializable_data.cpp
)

# Add minimal SDK sources
target_sources(${PLUGIN_NAME} PRIVATE
  ${VCS}/Samples/NvCodec/NvEncoder/NvEncoder.cpp
  ${VCS}/Samples/NvCodec/NvEncoder/NvEncoderCuda.cpp
  ${VCS}/Samples/Utils/NvCodecUtils.h
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE MONADO_REQUIRED)
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} 
                           ${CUDA_INCLUDE_DIRS} 
                           ${VCS}/Interface
                           ${VCS}/Samples/NvCodec
                           ${VCS}/Samples/NvCodec/NvEncoder
                           ${VCS}/Samples/Utils)

find_library(NVIDIA_ENCODE_LIB
NAMES nvidia-encode libnvidia-encode.so libnvidia-encode.so.1
PATHS
    /usr/lib/x86_64-linux-gnu
    /usr/lib
    /usr/local/lib
    /usr/lib64
)
                         
if (NOT NVIDIA_ENCODE_LIB)
  message(WARNING "libnvidia-encode not found. Install the NVIDIA driver userspace libs.")
endif()

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()
if(ILLIXR_FFmpeg_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${ILLIXR_FFmpeg_DEP_STR})
endif()

target_link_libraries(${PLUGIN_NAME} illixr_vulkan_utils spdlog::spdlog ${Vulkan_LIBRARIES})
target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} ${libavdevice_illixr_LIBRARIES} ${libavformat_illixr_LIBRARIES} ${libavcodec_illixr_LIBRARIES} ${libavutil_illixr_LIBRARIES})
target_link_libraries(${PLUGIN_NAME} ${NVIDIA_ENCODE_LIB} dl)
target_include_directories(${PLUGIN_NAME} PUBLIC ${libavcodec_illixr_INCLUDE_DIRS})
get_target_property(INC_DIRS ${PLUGIN_NAME} INCLUDE_DIRECTORIES)
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
