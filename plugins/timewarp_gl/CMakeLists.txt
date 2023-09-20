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
        shaders/basic_shader.hpp
        shaders/timewarp_shader.hpp
        utils/hmd.cpp
        utils/hmd.hpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/error_util.hpp
        ../../include/illixr/extended_window.hpp
        ../../include/illixr/global_module_defs.hpp
        ../../include/illixr/math_util.hpp
        ../../include/illixr/pose_prediction.hpp
        ../../include/illixr/shader_util.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        )

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${PLUGIN_NAME} PRIVATE -DUSE_GL)
install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
