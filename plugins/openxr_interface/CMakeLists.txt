set(CMAKE_CXX_STANDARD 17)
if(NOT ANDROID)
    message(WARNING "The openxr_interface plugin is currently supported on Android only.")
    set(USE_OPENXR_INTERFACE OFF)
    return()
endif()
set(PLUGIN_NAME plugin.openxr_interface${ILLIXR_BUILD_SUFFIX})

set(OCULUS_OPENXR_SDK "$ENV{OCULUS_OPENXR_MOBILE_SDK}")
if(NOT OCULUS_OPENXR_SDK)
    set(OCULUS_OPENXR_SDK "${CMAKE_SOURCE_DIR}/ovr_openxr_mobile_sdk")
endif()

find_package(OpenXR REQUIRED)
find_library(VULKAN_LIB vulkan)

# Locate glslc from the NDK shader-tools package.
find_program(GLSLC glslc
        PATHS "${ANDROID_NDK}/shader-tools/linux-x86_64"
        "${ANDROID_NDK}/shader-tools/windows-x86_64"
        REQUIRED
)

set(SPIRV_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SPIRV_DIR}")

# Helper: compile one GLSL file to SPIR-V, then wrap it in a C header.
function(compile_shader GLSL_FILE VARNAME OUT_HEADER)
    set(SPV_FILE "${SPIRV_DIR}/${VARNAME}.spv")
    add_custom_command(
            OUTPUT  "${SPV_FILE}"
            COMMAND ${GLSLC} --target-env=vulkan1.1 -O
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${GLSL_FILE}"
            -o "${SPV_FILE}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${GLSL_FILE}"
            VERBATIM
    )
    add_custom_command(
            OUTPUT  "${OUT_HEADER}"
            COMMAND ${CMAKE_COMMAND}
            -DINPUT=${SPV_FILE}
            -DOUTPUT=${OUT_HEADER}
            -DVARNAME=${VARNAME}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/spv_to_header.cmake"
            DEPENDS ${SPV_FILE}
            VERBATIM
    )
endfunction()

compile_shader(color.vert color_vert_spv "${SPIRV_DIR}/color_vert_spv.h")
compile_shader(color.frag color_frag_spv "${SPIRV_DIR}/color_frag_spv.h")
compile_shader(depth.frag depth_frag_spv "${SPIRV_DIR}/depth_frag_spv.h")
compile_shader(motion_vec.frag motion_vec_frag_spv "${SPIRV_DIR}/motion_vec_frag_spv.h")

add_library(${PLUGIN_NAME} SHARED
        plugin.cpp
        plugin.hpp
        stereo_renderer.cpp
        stereo_renderer.hpp
        oxr_relay.cpp
        oxr_relay.hpp
        ${SPIRV_DIR}/color_vert_spv.h   # ensures custom commands run
        ${SPIRV_DIR}/color_frag_spv.h
        ${SPIRV_DIR}/depth_frag_spv.h
        ${SPIRV_DIR}/motion_vec_frag_spv.h
        ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/quest3_params.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/misc.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/combined_pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/hand_interaction_pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/hand_pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/head_pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/openxr.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/palm_pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/pose_base.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/vulkan_context.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_context_provider.hpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/misc.cpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/combined_pose.cpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/hand_interaction_pose.cpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/hand_pose.cpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/head_pose.cpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/palm_pose.cpp
)

target_compile_definitions(${PLUGIN_NAME} PUBLIC
        USING_OPENXR
        ILLIXR_VULKAN                   # enables AHardwareBuffer path throughout ILLIXR
        XR_USE_GRAPHICS_API_VULKAN      # replaces XR_USE_GRAPHICS_API_OPENGL_ES
        XR_USE_PLATFORM_ANDROID
        COMBINED_ENCODING
)

target_include_directories(${PLUGIN_NAME} PUBLIC
        ${ANDROID_TOOLS_INCLUDE}
        ${ILLIXR_SOURCE_DIR}/include
        ${ANDROID_NDK}/sources/android/native_app_glue
        ${SPIRV_DIR}
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
        Boost::serialization
        Eigen3::Eigen
        android
        spdlog::spdlog_android
        OpenXR::openxr_loader
        ${VULKAN_LIB}          # libvulkan.so
        mediandk                     # AImageReader
)
