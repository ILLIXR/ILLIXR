# module to build and install the debugview ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "debugview is not yet available for windows")
    set(USE_ADA.DEBUGVIEW OFF)
else()


    set(PLUGIN_NAME plugin.debugview${ILLIXR_BUILD_SUFFIX})

    # source files, listed individually so that any changes will trigger a rebuild
    set(DEBUGVIEW_SOURCES plugin.cpp
        plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/shaders/demo_shader.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/gl_util/obj.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/shader_util.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
        $<TARGET_OBJECTS:illixr_imgui>
    )

    add_library(${PLUGIN_NAME} SHARED ${DEBUGVIEW_SOURCES})

    target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} Eigen3::Eigen dl ${GLEW_LIBRARIES} Threads::Threads spdlog::spdlog)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
