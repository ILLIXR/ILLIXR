if(ANDROID OR WIN32 OR MSVC)
    message(WARNING "boba_streaming_server is currently available only on Linux hosts")
    set(USE_BOBA_STREAMING_SERVER OFF)
    return()
endif()

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

set(PLUGIN_NAME plugin.boba_streaming_server${ILLIXR_BUILD_SUFFIX})
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

find_path(NVENC_INCLUDE_DIR nvEncodeAPI.h
          HINTS
          $ENV{VIDEO_CODEC_SDK_PATH}/Interface
          $ENV{VIDEO_CODEC_SDK_PATH}/include/ffnvcodec
          ${VIDEO_CODEC_SDK_PATH}/Interface
          ${VIDEO_CODEC_SDK_PATH}/include/ffnvcodec
          /usr/local/cuda/include
          /opt/nvidia/video-codec-sdk/Interface)
if(NOT NVENC_INCLUDE_DIR)
    message(FATAL_ERROR "NVENC headers not found. Set VIDEO_CODEC_SDK_PATH to the NVIDIA Video Codec SDK root.")
endif()

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/plugins/offload_rendering_server/nvenc/nvenc_encoder.cpp
            ${CMAKE_SOURCE_DIR}/plugins/offload_rendering_server/nvenc/nvenc_encoder.hpp
            ${CMAKE_SOURCE_DIR}/plugins/offload_rendering_server/nvenc/color_convert.cu
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/stereo_frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/quest3_params.hpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/frame.cpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/head_pose.cpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/misc.cpp)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
                           USING_OPENXR
                           NVENC_ENCODER
                           USE_AV1
                           COMBINED_ENCODING)
target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_SOURCE_DIR}/include
                           ${CMAKE_SOURCE_DIR}/plugins
                           ${NVENC_INCLUDE_DIR}
                           ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
                      Boost::serialization
                      CUDA::cuda_driver
                      CUDA::cudart
                      Eigen3::Eigen
                      Threads::Threads
                      spdlog::spdlog
                      ${CMAKE_DL_LIBS})
set_target_properties(${PLUGIN_NAME} PROPERTIES
                      CUDA_SEPARABLE_COMPILATION ON
                      CUDA_RESOLVE_DEVICE_SYMBOLS ON
                      CUDA_ARCHITECTURES "120")

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
