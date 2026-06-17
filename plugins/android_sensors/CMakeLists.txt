cmake_minimum_required(VERSION 3.22)

# sensor_encoder is Android-only (MediaCodec). See note in xr_sensor_capture
# about ANDROID variable availability after project().
if(NOT ANDROID)
    message(FATAL_ERROR "android_sensors requires an Android build target (MediaCodec)")
endif()

# XR_META_environment_depth is defined in the standard Khronos openxr.h
# (added in OpenXR SDK 1.1.36). No Meta SDK headers are needed.

find_package(OpenXR REQUIRED)
message(WARNING "OpenXR version: ${OpenXR_VERSION}")
find_library(CAMERA2_LIB camera2ndk)

set(PLUGIN_NAME plugin.android_sensors${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED
    plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${jnigraphics-lib}
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
    ${CAMERA2_LIB}
    mediandk
    android
    EGL
    GLESv3
    Eigen3::Eigen
    Boost::serialization
    spdlog::spdlog_android
    OpenXR::openxr_loader
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
    USING_OPENXR
    XR_USE_GRAPHICS_API_OPENGL_ES
    XR_USE_PLATFORM_ANDROID
)
