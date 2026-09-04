cmake_minimum_required(VERSION 3.22)

# sensor_encoder is Android-only (MediaCodec). See note in xr_sensor_capture
# about ANDROID variable availability after project().
if(NOT ANDROID)
    message(FATAL_ERROR "quest3.unity requires an Android build target (MediaCodec)")
endif()

find_package(OpenXR REQUIRED)
message(WARNING "OpenXR version: ${OpenXR_VERSION}")
find_library(CAMERA2_LIB camera2ndk)

set(PLUGIN_NAME plugin.quest3.unity${ILLIXR_BUILD_SUFFIX})

# Unity PluginAPI headers (IUnityInterface.h, IUnityGraphicsVulkan.h, etc.)
# Located in <Unity Editor>/Editor/Data/PluginAPI on the build machine.
# Set UNITY_PLUGIN_API_DIR on the CMake command line or in the Android Studio
# cmake arguments, e.g.:
#   -DUNITY_PLUGIN_API_DIR="C:/Program Files/Unity/Hub/Editor/6000.4.0f1/Editor/Data/PluginAPI"
if(NOT DEFINED UNITY_PLUGIN_API_DIR)
    message(FATAL_ERROR
        "UNITY_PLUGIN_API_DIR must be set to the Unity PluginAPI include directory.\n"
        "Example: -DUNITY_PLUGIN_API_DIR=\"C:/Program Files/Unity/Hub/Editor/6000.4.0f1/Editor/Data/PluginAPI\"")
endif()

add_library(${PLUGIN_NAME} SHARED
    plugin.cpp
    plugin.hpp
    ndk_encoder.cpp
    ndk_encoder.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/semantics.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/semantics.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/semantics.cpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${UNITY_PLUGIN_API_DIR}
    ${jnigraphics-lib}
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
    ${CAMERA2_LIB}
    mediandk
    android
    Eigen3::Eigen
    Boost::serialization
    spdlog::spdlog_android
    OpenXR::openxr_loader
    vulkan
)

target_compile_definitions(${PLUGIN_NAME} PRIVATE
    USING_OPENXR
    XR_USE_GRAPHICS_API_VULKAN
    XR_USE_PLATFORM_ANDROID
)

if(ANDROID_NATIVE_API_LEVEL LESS 26)
    message(FATAL_ERROR
        "xr_sensor_capture requires ANDROID_NATIVE_API_LEVEL >= 26 "
        "(current: ${ANDROID_NATIVE_API_LEVEL})")
endif()

set_target_properties(${PLUGIN_NAME} PROPERTIES
    CXX_STANDARD          17
    CXX_STANDARD_REQUIRED ON
)
