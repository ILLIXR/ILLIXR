find_package(glfw3 QUIET)
if (NOT glfw3_FOUND)
    pkg_search_module(glfw3 REQUIRED glfw3)
endif ()

find_package(OpenGL REQUIRED)

add_library(illixr_imgui STATIC
            imgui/imconfig.h
            imgui/imgui.cpp
            imgui/imgui_demo.cpp
            imgui/imgui_draw.cpp
            imgui/imgui.h
            imgui/imgui_internal.h
            imgui/imgui_tables.cpp
            imgui/imgui_widgets.cpp
            imgui/imstb_rectpack.h
            imgui/imstb_textedit.h
            imgui/imstb_truetype.h
            imgui/backends/imgui_impl_glfw.cpp
            imgui/backends/imgui_impl_glfw.h
            #imgui/backends/imgui_impl_glut.cpp
            #imgui/backends/imgui_impl_glut.h
            imgui/backends/imgui_impl_opengl3.cpp
            imgui/backends/imgui_impl_opengl3.h
            imgui/backends/imgui_impl_opengl3_loader.h
            #imgui/backends/imgui_impl_sdl2.cpp
            #imgui/backends/imgui_impl_sdl2.h
            #imgui/backends/imgui_impl_sdl3.cpp
            #imgui/backends/imgui_impl_sdl3.h
            #imgui/backends/imgui_impl_sdlrenderer2.cpp
            #imgui/backends/imgui_impl_sdlrenderer2.h
            #imgui/backends/imgui_impl_sdlrenderer3.cpp
            #imgui/backends/imgui_impl_sdlrenderer3.h
            #imgui/backends/imgui_impl_vulkan.cpp
            #imgui/backends/imgui_impl_vulkan.h
            imgui/misc/cpp/imgui_stdlib.cpp
            imgui/misc/cpp/imgui_stdlib.h
            #imgui/misc/fonts/binary_to_compressed_c.cpp
            #imgui/misc/freetype/imgui_freetype.cpp
            #imgui/misc/freetype/imgui_freetype.h
)

target_include_directories(illixr_imgui PUBLIC
                           ${glfw3_INCLUDE_DIRS}
                           ${gl_INCLUDE_DIRS}
                           ${X11_INCLUDE_DIR}
                           ${GLEW_INCLUDE_DIR}
                           ${GLU_INCLUDE_DIR}
)
target_link_libraries(illixr_imgui PUBLIC
                      ${X11_LIBRARIES}
                      ${GLEW_LIBRARIES}
                      ${glu_LDFLAGS}
                      glfw
                      OpenGL::GL
)
