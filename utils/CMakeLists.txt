add_library(illixr_hmd OBJECT
            hmd.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/hmd.hpp
)

target_include_directories(illixr_hmd PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(illixr_hmd_gl OBJECT
            hmd.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/hmd.hpp
)

target_include_directories(illixr_hmd_gl PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(illixr_hmd_gl PUBLIC USE_GL)

add_library(illixr_vulkan_utils STATIC
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
            vulkan_utils.cpp
)

target_include_directories(illixr_vulkan_utils PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(illixr_vulkan_utils PUBLIC
                      spdlog::spdlog
                      Boost::serialization
)
if(WIN32 OR MSVC)
    target_link_libraries(illixr_vulkan_utils PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()
if(UNIX)
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
    pkg_search_module(GLIB REQUIRED glib-2.0)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
    pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)
    pkg_check_modules(GST_AUDIO REQUIRED gstreamer-audio-1.0)

    set(INC_DIRS ${GLIB_INCLUDE_DIRS} ${GSTREAMER_INCLUDE_DIRS} ${GST_APP_INCLUDE_DIRS} ${GST_AUDIO_INCLUDE_DIRS} ${GST_VIDEO_INCLUDE_DIRS})
    # remove duplicates
    list(REMOVE_DUPLICATES INC_DIRS)

    # gather the libraries
    set(LINK_LIBS ${GLIB_LIBRARIES} ${GSTREAMER_LIBRARIES} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES} ${GST_AUDIO_LIBRARIES})
    # reverse to maintain order in final list
    list(REVERSE LINK_LIBS)
    # remove the copious duplicates
    list(REMOVE_DUPLICATES LINK_LIBS)
    # reverse back to original order
    list(REVERSE LINK_LIBS)

    add_library(video_encoder_ada OBJECT
                video_encoder.hpp
                video_encoder.cpp
    )

    target_include_directories(video_encoder_ada PUBLIC
                               ${OpenCV_INCLUDE_DIRS}
                               ${INC_DIRS}
    )

    target_link_libraries(video_encoder_ada PUBLIC
                          ${OpenCV_LIBS}
                          ${LINK_LIBS}
    )

    target_compile_definitions(video_encoder_ada PUBLIC ADA)

    add_library(video_decoder_ada OBJECT
                video_decoder.hpp
                video_decoder.cpp
    )

    target_include_directories(video_decoder_ada PUBLIC
                               ${OpenCV_INCLUDE_DIRS}
                               ${INC_DIRS}
    )

    target_link_libraries(video_decoder_ada PUBLIC
                          ${OpenCV_LIBS}
                          ${LINK_LIBS}
    )
    target_compile_definitions(video_decoder_ada PUBLIC ADA)


    add_library(video_encoder_vio OBJECT
                video_encoder.hpp
                video_encoder.cpp
    )

    target_include_directories(video_encoder_vio PUBLIC
                               ${OpenCV_INCLUDE_DIRS}
                               ${INC_DIRS}
    )

    target_link_libraries(video_encoder_vio PUBLIC
                          ${OpenCV_LIBS}
                          ${LINK_LIBS}
    )

    target_compile_definitions(video_encoder_vio PUBLIC VIO)

    add_library(video_decoder_vio OBJECT
                video_decoder.hpp
                video_decoder.cpp
    )

    target_include_directories(video_decoder_vio PUBLIC
                               ${OpenCV_INCLUDE_DIRS}
                               ${INC_DIRS}
    )

    target_link_libraries(video_decoder_vio PUBLIC
                          ${OpenCV_LIBS}
                          ${LINK_LIBS}
    )
    target_compile_definitions(video_decoder_vio PUBLIC VIO)
endif()
