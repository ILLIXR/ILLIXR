# module to build and install the debugview ILLIXR plugin

find_package(glfw3 REQUIRED)

set(PLUGIN_NAME plugin.debugview${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
set(DEBUGVIEW_SOURCES plugin.cpp
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_impl_glfw.cpp
        imgui/imgui_impl_opengl3.cpp
        imgui/imgui_widgets.cpp
        imgui/imconfig.h
        imgui/imgui.h
        imgui/imgui_impl_glfw.h
        imgui/imgui_impl_opengl3.h
        imgui/imgui_internal.h
        imgui/imstb_rectpack.h
        imgui/imstb_textedit.h
        imgui/imstb_truetype.h
        shaders/demo_shader.hpp
        ../../include/illixr/data_format.hpp
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
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} ${glfw3_LIBRARIES} ${gl_LIBRARIES} ${Eigen3_LIBRARIES} dl pthread)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
