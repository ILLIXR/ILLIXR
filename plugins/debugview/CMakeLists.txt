# module to build and install the debugview ILLIXR plugin

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    pkg_search_module(glfw3 REQUIRED glfw3)
endif()

find_package(OpenGL REQUIRED)

set(PLUGIN_NAME plugin.debugview${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
set(DEBUGVIEW_SOURCES plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imconfig.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_glfw.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_opengl3.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_internal.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imstb_rectpack.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imstb_textedit.h
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imstb_truetype.h
    ${CMAKE_SOURCE_DIR}/include/illixr/shaders/demo_shader.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/misc/cpp/imgui_stdlib.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/imgui/misc/cpp/imgui_stdlib.h
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/gl_util/obj.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/pose_prediction.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/shader_util.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

add_library(${PLUGIN_NAME} SHARED ${DEBUGVIEW_SOURCES})

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} glfw OpenGL::GL ${Eigen3_LIBRARIES} dl Threads::Threads spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
