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
    third_party/imgui.cpp
    third_party/imgui_demo.cpp
    third_party/imgui_draw.cpp
    third_party/imgui_impl_glfw.cpp
    third_party/imgui_impl_opengl3.cpp
    third_party/imgui_widgets.cpp
    third_party/imconfig.h
    third_party/imgui.h
    third_party/imgui_impl_glfw.h
    third_party/imgui_impl_opengl3.h
    third_party/imgui_internal.h
    third_party/imstb_rectpack.h
    third_party/imstb_textedit.h
    third_party/imstb_truetype.h
    ../../include/illixr/shaders/demo_shader.hpp
    ../../include/illixr/data_format.hpp
    ../../include/illixr/opencv_data_types.hpp
    ../../include/illixr/error_util.hpp
    ../../include/illixr/gl_util/obj.hpp
    ../../include/illixr/global_module_defs.hpp
    ../../include/illixr/math_util.hpp
    ../../include/illixr/pose_prediction.hpp
    ../../include/illixr/shader_util.hpp
    ../../include/illixr/switchboard.hpp
    ../../include/illixr/threadloop.hpp
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
