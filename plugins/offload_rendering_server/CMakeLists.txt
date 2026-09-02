if(ANDROID)
    message(WARNING "offload_rendering_server is not available for Android")
    set(USE_OFFLOAD_RENDERING_SERVER OFF)
    return()
endif()
#set(DUMP_FRAMES ON)
# Enable CUDA language only if using NVENC encoder
option(USE_NVENC_ENCODER "Use direct NVENC encoder instead of FFmpeg" OFF)
option(USE_OPENXR_DEFINITIONS "Use the OpenXR pose definitions" OFF)

# NVENC is required for windows, ffmpeg is not supported on that platform.
if(WIN32 OR MSVC)
    set(USE_NVENC_ENCODER ON)
endif()

if(USE_NVENC_ENCODER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    find_package(CUDAToolkit REQUIRED)
endif()

if(MSVC)
    # Remove /RTC1 from debug flags - incompatible with optimization flags in CUDA
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    set(SKIP_OPENXR_APP ON)
endif()

# Note that the offload rendering server only supports Monado (not native)
get_external_for_plugin(Monado_vk)
set(PLUGIN_NAME "plugin.offload_rendering_server${ILLIXR_BUILD_SUFFIX}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Common source files
set(COMMON_SOURCES
    plugin.cpp
    offload_rendering_server.cpp
    pose_relay.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/misc.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/frame.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/combined_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/hand_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/hand_interaction_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/head_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/palm_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/hmd_config.cpp
)

set(COMMON_HEADERS
    plugin.hpp
    offload_rendering_server.hpp
    pose_relay.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/hmd_config.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/latency_data.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/combined_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/hand_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/hand_interaction_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/head_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/palm_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/pose_base.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/frame.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/misc.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/combined_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/hand_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/hand_interaction_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/head_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/hmd_config.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/palm_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/pose_base.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

if(USE_NVENC_ENCODER)
    # NVENC encoder sources
    set(NVENC_SOURCES
        nvenc/nvenc_encoder.cpp
        nvenc/color_convert.cu
    )
    if(DUMP_FRAMES)
        set(FRAME_DUMPER_FILES
            nvenc/frame_saver.cpp
            nvenc/frame_saver.hpp
            nvenc/frame_saver_integration.hpp
        )
    else()
        set(FRAME_DUMPER_FILES "")
    endif()
    set(NVENC_HEADERS
        nvenc/nvenc_encoder.hpp
    )

    add_library(${PLUGIN_NAME} SHARED
                ${COMMON_SOURCES}
                ${COMMON_HEADERS}
                ${NVENC_SOURCES}
                ${NVENC_HEADERS}
                ${FRAME_DUMPER_FILES}
    )

    # Define NVENC_ENCODER macro
    target_compile_definitions(${PLUGIN_NAME} PRIVATE NVENC_ENCODER ENABLE_MONADO)

    if(DUMP_FRAMES)
        target_compile_definitions(${PLUGIN_NAME} PRIVATE DUMP_FRAMES)
    endif()
    # Set CUDA properties
    # Set CUDA architectures (75=Turing, 86=Ampere, 89=Ada)
    set_target_properties(${PLUGIN_NAME} PROPERTIES
                          CUDA_SEPARABLE_COMPILATION ON
                          CUDA_RESOLVE_DEVICE_SYMBOLS ON
                          CUDA_ARCHITECTURES "75;86;89"
    )

    # CXX-only compile options (not CUDA)
    if(WIN32 OR MSVC)
        if(MSVC)
            target_compile_options(${PLUGIN_NAME} PRIVATE
                                   $<$<COMPILE_LANGUAGE:CXX>:/W3>
                                   $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
                                   $<$<COMPILE_LANGUAGE:CXX>:${MSVC_RUNTIME_FLAG}>
                                   $<$<COMPILE_LANGUAGE:CXX>:/DNOMINMAX>
                                   $<$<COMPILE_LANGUAGE:CXX>:/DWIN32_LEAN_AND_MEAN>
            )
        else()
            target_compile_options(${PLUGIN_NAME} PRIVATE
                                   $<$<COMPILE_LANGUAGE:CXX>:-DNOMINMAX>
                                   $<$<COMPILE_LANGUAGE:CXX>:-DWIN32_LEAN_AND_MEAN>
            )
        endif()
    endif()

    set(CODEC_SDK_FOUND false)

    if(DEFINED ENV{VIDEO_CODEC_SDK_PATH})
        if(IS_DIRECTORY $ENV{VIDEO_CODEC_SDK_PATH})
            target_include_directories(${PLUGIN_NAME} PUBLIC
                                       $ENV{VIDEO_CODEC_SDK_PATH}/Interface
            )
            set(CODEC_SDK_FOUND true)
        else()
            message(WARNING "The VIDEO_CODEC_SDK_PATH environment variable was set, but is not a valid directory.")
        endif()
    endif()

    if(NOT CODEC_SDK_FOUND AND VIDEO_CODEC_SDK_PATH)
        if(IS_DIRECTORY ${VIDEO_CODEC_SDK_PATH})
            target_include_directories(${PLUGIN_NAME} PUBLIC
                                       ${VIDEO_CODEC_SDK_PATH}/Interface
            )
            set(CODEC_SDK_FOUND true)
        else()
            message(WARNING "The VIDEO_CODEC_SDK_PATH was given on the command line, but it is not a directory.")
        endif()
    endif()

    if(UNIX)
        find_path(NVENC_INCLUDE_DIR nvEncodeAPI.h
                  PATHS
                  /usr/local/cuda/include
                  /opt/nvidia/video-codec-sdk/Interface
                  ${CMAKE_CURRENT_SOURCE_DIR}/../external/Video_Codec_SDK/Interface
                  $ENV{VIDEO_CODEC_SDK_PATH}/Interface
                  ${VIDEO_CODEC_SDK_PATH}/Interface
        )
        if(NVENC_INCLUDE_DIR)
            target_include_directories(${PLUGIN_NAME} PUBLIC ${NVENC_INCLUDE_DIR})
            set(CODEC_SDK_FOUND true)
        else()
            message(FATAL_ERROR "NVENC headers not found. Set VIDEO_CODEC_SDK_PATH environment variable.")
        endif()
    endif()

    if(NOT CODEC_SDK_FOUND)
        message(FATAL_ERROR "VIDEO_CODEC_SDK_PATH not set. Set it to your Video Codec SDK location.")
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC
                               ${CUDAToolkit_INCLUDE_DIRS}
    )
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          CUDA::cudart
                          CUDA::cuda_driver
    )
    message(STATUS "Building offload_rendering_server with NVENC encoder (no FFmpeg)")
    add_compile_definitions(USING_MONADO)
else()
    # ffmpeg
    get_external_for_plugin(ILLIXR_FFmpeg)

    add_compile_definitions(ILLIXR_LIBAV)

    add_library(${PLUGIN_NAME} SHARED
                ${COMMON_SOURCES}
                ${COMMON_HEADERS}
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/ffmpeg_utils.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
    )

    if(TARGET ILLIXR_FFmpeg_ext)
        add_dependencies(${PLUGIN_NAME} ILLIXR_FFmpeg_ext)
    endif()

    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          ${libavdevice_illixr_LIBRARIES}
                          ${libavformat_illixr_LIBRARIES}
                          ${libavcodec_illixr_LIBRARIES}
                          ${libavutil_illixr_LIBRARIES}
    )

    target_include_directories(${PLUGIN_NAME} PUBLIC ${libavcodec_illixr_INCLUDE_DIRS})

    message(STATUS "Building offload_rendering_server with FFmpeg encoder")
endif()

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      spdlog::spdlog
                      illixr_vulkan_utils
                      Eigen3::Eigen
                      ${Vulkan_LIBRARIES}
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE MONADO_REQUIRED USE_AV1)

target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC
                           ${Vulkan_INCLUDE_DIRS}
                           ${ILLIXR_SOURCE_DIR}/include
                           ${XRT_SOURCE_DIR}/src/xrt
                           ${XRT_SOURCE_DIR}/src/xrt/include
                           ${XRT_SOURCE_DIR}/src/xrt/auxiliary
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
                               BUILDING_LIBRARY
                               VK_USE_PLATFORM_WIN32_KHR
                               WIN32_LEAN_AND_MEAN
                               NOMINMAX
                               COMBINED_ENCODING
                               NVENC_ENCODER
    )
else()
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
                               VK_USE_PLATFORM_XLIB_KHR
    )

    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          dl
                          pthread
    )

    set_property(TARGET ${PLUGIN_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_compile_definitions(${PLUGIN_NAME} PUBLIC ENABLE_MONADO)
if(USE_OPENXR_DEFINITIONS)
    target_compile_definitions(${PLUGIN_NAME} PUBLIC USING_OPENXR OPENXR_CLIENT)
endif()
get_target_property(INC_DIRS ${PLUGIN_NAME} INCLUDE_DIRECTORIES)
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)

# Print configuration summary
message(STATUS "=== offload_rendering_server Configuration ===")
message(STATUS "  Encoder backend: ${USE_NVENC_ENCODER}")
if(USE_NVENC_ENCODER)
    message(STATUS "    - Using direct NVENC (no FFmpeg)")
else()
    message(STATUS "    - Using FFmpeg with NVENC")
endif()
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "=============================================")
