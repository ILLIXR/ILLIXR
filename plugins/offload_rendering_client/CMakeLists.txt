cmake_minimum_required(VERSION 3.16)
if(WIN32 OR MSVC)
    message(WARNING "offload_rendering_client is not yet available for windows")
    set(USE_OFFLOAD_RENDERING_CLIENT OFF)
    return()
endif()

    set(PLUGIN_NAME plugin.offload_rendering_client${ILLIXR_BUILD_SUFFIX})

if(ANDROID)
    find_library(VULKAN_LIB vulkan)
else()
    # ffmpeg
    get_external_for_plugin(ILLIXR_FFmpeg)

    find_package(CUDAToolkit REQUIRED)

    add_compile_definitions(ILLIXR_LIBAV)

    # Check if the modern target exists; if not, fall back to the old one
    if(NOT TARGET CUDA::nvToolsExt AND TARGET CUDA::nvtx3)
        add_library(CUDA::nvToolsExt INTERFACE IMPORTED)
        target_link_libraries(CUDA::nvToolsExt INTERFACE CUDA::nvtx3)
    endif()
endif()

set(SOURCE_FILES
    plugin.cpp
    offload_rendering_client.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/misc.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/head_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/frame.cpp
)

set(HEADER_FILES
    offload_rendering_client.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/hand_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/misc.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/head_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/pose_base.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/frame.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

if(ANDROID)
    set(PLATFORM_SOURCE_FILES
        android/frame_decoder.cpp
        android/stereo_surface_decoder.cpp

    )

    set(PLATFORM_HEADER_FILES
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/vulkan_context.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_context_provider.hpp
        android/frame_decoder.hpp
        android/stereo_surface_decoder.hpp
        android/jni_helper.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/quest3_params.hpp
    )

else()
    set(PLATFORM_SOURCE_FILES)

    set(PLATFORM_HEADER_FILES
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/ffmpeg_utils.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
    )
endif()

add_library(${PLUGIN_NAME} SHARED
            ${SOURCE_FILES}
            ${HEADER_FILES}
            ${PLATFORM_SOURCE_FILES}
            ${PLATFORM_HEADER_FILES}
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Boost::serialization
                      Eigen3::Eigen
)

if(ANDROID)
    set(OCULUS_OPENXR_SDK "$ENV{OCULUS_OPENXR_MOBILE_SDK}")
    if(NOT OCULUS_OPENXR_SDK)
        set(OCULUS_OPENXR_SDK "${CMAKE_SOURCE_DIR}/ovr_openxr_mobile_sdk")
    endif()

    find_package(OpenXR REQUIRED)
    target_compile_definitions(${PLUGIN_NAME} PUBLIC
                               USING_OPENXR
                               ILLIXR_VULKAN               # enables AHardwareBuffer path in frame.hpp and offload_rendering_client
                               XR_USE_PLATFORM_ANDROID
                               COMBINED_ENCODING
                               USE_AV1
    )
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          spdlog::spdlog_android
                          mediandk
                          android
                          OpenXR::headers
                          ${VULKAN_LIB}
    )
    target_include_directories(${PLUGIN_NAME} PUBLIC
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )
else()

    if(TARGET ILLIXR_FFmpeg_ext)
        add_dependencies(${PLUGIN_NAME} ILLIXR_FFmpeg_ext)
        message(WARNING "ADD DEP clie")
    endif()

    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          illixr_vulkan_utils
                          spdlog::spdlog
                          ${Vulkan_LIBRARIES}
                          ${libavdevice_illixr_LIBRARIES}
                          ${libavformat_illixr_LIBRARIES}
                          ${libavcodec_illixr_LIBRARIES}
                          ${libavutil_illixr_LIBRARIES}
                          ${libswscale_illixr_LIBRARIES}
                          CUDA::cudart
                          CUDA::nppif
                          CUDA::nppial
                          CUDA::nppicc
                          CUDA::nppidei
                          CUDA::nppig
                          CUDA::nppim
                          CUDA::nppist
                          CUDA::nppisu
                          CUDA::nppitc
                          CUDA::npps
                          CUDA::nvToolsExt
    )
    target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC
                               ${Vulkan_INCLUDE_DIRS}
    )
    target_include_directories(${PLUGIN_NAME} PUBLIC
                               ${libavcodec_illixr_INCLUDE_DIRS}
                               ${CUDAToolkit_INCLUDE_DIRS}
    )
endif()

if(NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
