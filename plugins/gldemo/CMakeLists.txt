# module to build and install the gldemo ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "gldemo is not yet available for windows")
    set(USE_GLDEMO OFF)
else()

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

    target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)
    target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} Eigen3::Eigen spdlog::spdlog)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
