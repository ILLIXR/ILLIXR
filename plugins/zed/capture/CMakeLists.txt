cmake_minimum_required(VERSION 3.22)
if (POLICY CMP0153)
    cmake_policy(SET CMP0153 OLD)
endif()

if (POLICY CMP0146)
    cmake_policy(SET CMP0146 OLD)
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(-Wno-deprecated-declarations)

find_package(OpenCV 4 REQUIRED)
find_package(ZED 3 REQUIRED)
find_package(CUDA ${ZED_CUDA_VERSION} EXACT REQUIRED)
find_package(Boost REQUIRED CONFIG COMPONENTS filesystem)

set(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY})

find_package(PkgConfig)
find_package(Eigen3 REQUIRED)
set(ENABLE_OPENCV ON)
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    pkg_search_module(glfw3 REQUIRED glfw3)
endif()
find_package(X11 REQUIRED)
pkg_check_modules(glu REQUIRED glu)
find_package(GLEW REQUIRED)
pkg_check_modules(gl REQUIRED gl)

find_package(OpenGL REQUIRED)

add_executable(zed_capture
               capture.hpp
               capture.cpp
               capture_main.cpp
               cxxopts.hpp
               zed_opencv.hpp
               files.hpp
               files.cpp
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

)

add_compile_definitions(-DHAVE_ZED)
target_link_libraries(zed_capture PUBLIC
                      ${ZED_LIBS}
                      ${X11_LIBRARIES}
                      ${GLEW_LIBRARIES}
                      ${glu_LDFLAGS}
                      ${OpenCV_LIBRARIES}
                      glfw
                      OpenGL::GL
                      Eigen3::Eigen
                      dl
                      Threads::Threads
                      boost_filesystem
)
target_include_directories(zed_capture PUBLIC
                           ${CMAKE_SOURCE_DIR}/include
                           ${ZED_INCLUDE_DIRS}
                           ${X11_INCLUDE_DIR}
                           ${GLEW_INCLUDE_DIR}
                           ${GLU_INCLUDE_DIR}
                           ${OpenCV_INCLUDE_DIRS}
                           ${glfw3_INCLUDE_DIRS}
                           ${gl_INCLUDE_DIRS}
                           ${BOOST_INCLUDE_DIRS}
)

install(TARGETS zed_capture DESTINATION bin)
