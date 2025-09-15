add_library(illixr_hmd OBJECT hmd.cpp
            hmd.hpp
)

target_include_directories(illixr_hmd PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(illixr_hmd_gl OBJECT hmd.cpp
            hmd.hpp
)

target_include_directories(illixr_hmd_gl PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(illixr_hmd_gl PUBLIC USE_GL)

add_library(illixr_vulkan_utils STATIC vulkan_utils.cpp
            vulkan_utils.hpp
)

target_include_directories(illixr_vulkan_utils PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(illixr_vulkan_utils PUBLIC
            spdlog::spdlog 
            Boost::serialization
)
if(WIN32 OR MSVC)
    target_link_libraries(illixr_vulkan_utils PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()
#[[
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
            #imgui/backends/imgui_impl_glfw.cpp
            #imgui/backends/imgui_impl_glfw.h
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
]]