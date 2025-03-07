# module to build and install the timewarp_gl ILLIXR plugin
if(NOT DEFINED BUILDING_ILLIXR)
    message(ERROR "The timewarp_gl plugin cannot be built outside of the main ILLIXR build")
endif()
get_external_for_plugin(Vulkan)

set(PLUGIN_NAMES "plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX}")
set(PLUGIN_DEFINITIONS "x")
set(PLUGIN_SOURCES "plugin.cpp")

if(ENABLE_MONADO)
    get_external_for_plugin(Monado_gl)
    file(COPY_FILE ${CMAKE_SOURCE_DIR}/plugins/timewarp_gl/plugin.cpp ${CMAKE_BINARY_DIR}/plugins/timewarp_gl/plugin_m.cpp)
    list(APPEND PLUGIN_NAMES "plugin.timewarp_gl.monado${ILLIXR_BUILD_SUFFIX}")
    list(APPEND PLUGIN_DEFINITIONS "ENABLE_MONADO")
    list(APPEND PLUGIN_SOURCES "${CMAKE_BINARY_DIR}/plugins/timewarp_gl/plugin_m.cpp")
endif()

foreach(item IN ZIP_LISTS PLUGIN_NAMES PLUGIN_DEFINITIONS PLUGIN_SOURCES)
    set(PLUGIN_NAME "${item_0}")

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED
                ${item_2}
                plugin.hpp
                shaders/timewarp_shader.hpp
                $<TARGET_OBJECTS:illixr_hmd>
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

    if(NOT ${item_1} STREQUAL "x")
        target_compile_definitions(${PLUGIN_NAME} PUBLIC ${item_1})
        target_compile_definitions(${PLUGIN_NAME} PRIVATE MONADO_REQUIRED)
        target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} spdlog::spdlog ${Vulkan_LIBRARY})
    if(Vulkan_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
    endif()
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE -DUSE_GL)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endforeach()
