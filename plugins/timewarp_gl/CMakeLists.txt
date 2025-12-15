# module to build and install the timewarp_gl ILLIXR plugin
find_package(glfw3 QUIET)
if (NOT glfw3_FOUND)
    pkg_search_module(glfw3 REQUIRED glfw3)
endif ()

if(NOT PLUGIN_NAME)
    set(PLUGIN_NAME "plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX}")
    set(PLUGIN_SOURCE "plugin.cpp")
    set(TW_PREFIX "")
endif()

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED
            ${PLUGIN_SOURCE}
            ${TW_PREFIX}plugin.hpp
            ${TW_PREFIX}shaders/timewarp_shader.hpp
            $<TARGET_OBJECTS:illixr_hmd_gl> ?? LX
 $<TARGET_OBJECTS:illixr_hmd> ?? AND
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/extended_window.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/shader_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

if(TW_PLUGIN_DEFINITIONS)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE ${TW_PLUGIN_DEFINITIONS})
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

if (ANDROID)
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${ILLIXR_SOURCE_DIR}/include
                               ${glfw3_INCLUDE_DIRS}
                               ${OpenCV_LIBS}
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )
else()
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${X11_INCLUDE_DIR}
                               ${GLEW_INCLUDE_DIR}
                               ${GLU_INCLUDE_DIR}
                               ${gl_INCLUDE_DIRS}
                               ${ILLIXR_SOURCE_DIR}/include
                               ${Vulkan_INCLUDE_DIRS}
    )
endif()

if (ANDROID)
    target_link_libraries(${PLUGIN_NAME}
                          plugin.extended_window${ILLIXR_BUILD_SUFFIX}
                          vulkan
                          EGL
                          GLESv3
                          android
                          Eigen3::Eigen
                          Boost::serialization
                          spdlog::spdlog_android
    )
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          ${X11_LIBRARIES}
                          ${GLEW_LIBRARIES}
                          ${glu_LDFLAGS}
                          ${gl_LIBRARIES}
                          Eigen3::Eigen
                          spdlog::spdlog
                          ${Vulkan_LIBRARY}
    )
endif()

if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
if (NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
