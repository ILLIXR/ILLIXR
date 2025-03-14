# module to build and install the timewarp_gl ILLIXR plugin
if(USE_MONADO)
    get_external_for_plugin(Monado_gl)
endif()
if(NOT DEFINED BUILDING_ILLIXR)
    message(ERROR "The timewarp_gl plugin cannot be built outside of the main ILLIXR build")
endif()

set(PLUGIN_NAME plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            shaders/timewarp_shader.hpp
            utils/hmd.cpp
            utils/hmd.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/extended_window.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/shader_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${PLUGIN_NAME} PRIVATE -DUSE_GL)
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
