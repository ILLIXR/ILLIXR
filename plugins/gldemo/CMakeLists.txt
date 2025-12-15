# module to build and install the gldemo ILLIXR plugin

set(PLUGIN_NAME plugin.gldemo${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/shaders/demo_shader.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/extended_window.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/gl_util/obj.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/shader_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

if (ANDROID)
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${ILLIXR_SOURCE_DIR}/include
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue)
else()
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${X11_INCLUDE_DIR}
                               ${GLEW_INCLUDE_DIR}
                               ${GLU_INCLUDE_DIR}
                               ${gl_INCLUDE_DIRS}
                               ${ILLIXR_SOURCE_DIR}/include
    )
endif()

if (ANDROID)
    target_link_libraries(${PLUGIN_NAME}
                          plugin.extended_window${ILLIXR_BUILD_SUFFIX}
                          EGL
                          GLESv3
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
    )
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if (NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
