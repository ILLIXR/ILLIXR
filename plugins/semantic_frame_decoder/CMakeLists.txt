cmake_minimum_required(VERSION 3.24)

set(PLUGIN_NAME plugin.semantic_frame_decoder${ILLIXR_BUILD_SUFFIX})

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.27.0")
    cmake_minimum_required(VERSION 3.27)
endif()
set(CMAKE_CUDA_ARCHITECTURES native)

# ---- NVDEC / CUDA ----
# NVDEC_SDK_DIR must point to the NVIDIA Video Codec SDK root, e.g.:
#   -DNVDEC_SDK_DIR="C:/dev/NvidiaVideoCodecSDK"
# Available from https://developer.nvidia.com/nvidia-video-codec-sdk
# Supports SDK 12.x and 13.x.
if(NOT DEFINED NVDEC_SDK_DIR)
    message(FATAL_ERROR
            "NVDEC_SDK_DIR must be set to the NVIDIA Video Codec SDK root.\n"
            "Example: -DNVDEC_SDK_DIR=\"C:/dev/NvidiaVideoCodecSDK\"")
endif()

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

# Detect AV1 decode support from the SDK headers
include(CheckCXXSymbolExists)
set(CMAKE_REQUIRED_INCLUDES "${NVDEC_SDK_DIR}/Interface")
check_cxx_symbol_exists(cudaVideoCodec_AV1 "cuviddec.h" NVDEC_HAS_AV1)
unset(CMAKE_REQUIRED_INCLUDES)

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            nv12_to_rgb.cu
            nv12_to_rgb.cuh
            nvdec_decoder.hpp
            nvdec_decoder.cpp
            ${NVDEC_SDK_DIR}/Samples/NvCodec/NvDecoder/NvDecoder.cpp
            ${NVDEC_SDK_DIR}/Samples/NvCodec/NvDecoder/NvDecoder.h
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/semantics.hpp
            ${CMAKE_BINARY_DIR}/include/illixr/bridge/semantic_xr/semantic_data.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/semantics.hpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/semantics.cpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES
                      CUDA_SEPARABLE_COMPILATION ON
)

target_include_directories(${PLUGIN_NAME} PRIVATE
                           ${ILLIXR_SOURCE_DIR}/include
                           ${CMAKE_BINARY_DIR}/include
                           ${NVDEC_SDK_DIR}/Interface        # cuviddec.h, nvcuvid.h
                           ${NVDEC_SDK_DIR}/Samples/NvCodec  # NvDecoder.h, NvCodecUtils.h
                           ${NVDEC_SDK_DIR}/Samples/Utils
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Eigen3::Eigen
                      Boost::serialization
                      spdlog::spdlog
                      CUDA::cuda_driver
                      CUDA::cudart
)

# Link nvcuvid - name differs by platform
if(WIN32)
    target_link_libraries(${PLUGIN_NAME} PUBLIC nvcuvid)
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          ${NVDEC_SDK_DIR}/Lib/linux/stubs/x86_64/libnvcuvid.so)
endif()

target_compile_definitions(${PLUGIN_NAME} PRIVATE
                           $<$<BOOL:${NVDEC_HAS_AV1}>:NVDEC_HAS_AV1>
)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
