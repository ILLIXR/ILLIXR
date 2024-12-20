find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    pkg_search_module(glfw3 REQUIRED glfw3)
endif()

find_package(OpenGL REQUIRED)

set(PLUGIN_NAME plugin.hand_tracking.viewer${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            imgui/imconfig.h
            imgui/imgui.cpp
            imgui/imgui.h
            imgui/imgui_demo.cpp
            imgui/imgui_draw.cpp
            imgui/imgui_internal.h
            imgui/imgui_tables.cpp
            imgui/imgui_widgets.cpp
            imgui/imstb_rectpack.h
            imgui/imstb_textedit.h
            imgui/imstb_truetype.h
            imgui/misc/cpp/imgui_stdlib.cpp
            imgui/misc/cpp/imgui_stdlib.h
            imgui/backends/imgui_impl_glfw.h
            imgui/backends/imgui_impl_glfw.cpp
            imgui/backends/imgui_impl_opengl3.h
            imgui/backends/imgui_impl_opengl3.cpp
            ../../../include/illixr/data_format/hand_tracking_data.hpp
            ../../../include/illixr/data_format/opencv_data_types.hpp
            ../../../include/illixr/threadloop.hpp
            ../../../include/illixr/switchboard.hpp
)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()
#target_compile_definitions(${PLUGIN_NAME} PUBLIC -DVIEW_DUMP)
target_include_directories(${PLUGIN_NAME} PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PUBLIC ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} glfw OpenGL::GL ${Eigen3_LIBRARIES} dl Threads::Threads)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)