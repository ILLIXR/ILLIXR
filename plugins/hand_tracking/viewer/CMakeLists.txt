find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    pkg_search_module(glfw3 REQUIRED glfw3)
endif()

find_package(OpenGL REQUIRED)

set(PLUGIN_NAME plugin.hand_tracking.viewer${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imconfig.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_demo.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_draw.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_internal.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_tables.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imgui_widgets.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imstb_rectpack.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imstb_textedit.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/imstb_truetype.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/misc/cpp/imgui_stdlib.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/misc/cpp/imgui_stdlib.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_glfw.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_glfw.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_opengl3.h
            ${CMAKE_SOURCE_DIR}/include/illixr/imgui/backends/imgui_impl_opengl3.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/hand_tracking_data.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()
#target_compile_definitions(${PLUGIN_NAME} PUBLIC -DVIEW_DUMP)
target_include_directories(${PLUGIN_NAME} PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS} ${glfw3_INCLUDE_DIRS} ${gl_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PUBLIC ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${OpenCV_LIBRARIES} glfw OpenGL::GL ${Eigen3_LIBRARIES} dl Threads::Threads)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)