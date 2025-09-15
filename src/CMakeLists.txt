
add_compile_definitions(VK_USE_PLATFORM_XLIB_KHR)

set(VULKAN_FILES
    src/display/x11_direct.hpp
    src/display/x11_direct.cpp
    src/display/glfw_extended.hpp
    src/display/glfw_extended.cpp
    src/display/headless.hpp
    src/display/headless.cpp
    ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_objects.hpp
)

add_executable(main${ILLIXR_BUILD_SUFFIX}
               src/main.cpp
               src/illixr.hpp 
               src/cxxopts.hpp
               ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
               ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp 
)

add_library(plugin.main${ILLIXR_BUILD_SUFFIX} SHARED
            src/plugin.cpp
            src/runtime_impl.cpp
            src/illixr.hpp
            src/cxxopts.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${VULKAN_FILES}
            $<TARGET_OBJECTS:illixr_vulkan_utils>
)

if(WIN32 OR MSVC) 
    target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE BUILDING_LIBRARY)
endif()
##############
# yaml parser
##############
if(BUILD_OXR_INTERFACE)
    target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC -DOXR_INTERFACE)
endif()

get_external(Yamlcpp)

target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE "ILLIXR_VISUALIZERS='${VIS_NAMES}'")

target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE ILLIXR_INSTALL_PATH="${CMAKE_INSTALL_PREFIX}")

target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                           ${SQLite3_INCLUDE_DIR}
                           ${yaml-cpp_INCLUDE_DIRS}
                           #${X11_INCLUDE_DIR}
                           ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include
                           ${CMAKE_SOURCE_DIR}/include
)
target_link_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                      ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib
)
target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                      Threads::Threads
                      ${yaml-cpp_LIBRARIES}
                      spdlog::spdlog
                      ${SQLite3_LIBRARIES}
                      Boost::serialization
                      X11
                      glfw
)

if(WIN32 OR MSVC)
    target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                          GLEW::GLEW
                          OpenGL::GL
                          Vulkan::Vulkan
                          GPUOpen::VulkanMemoryAllocator
    )
else()

    if(Vulkan_DEP_STR)
        add_dependencies(plugin.main${ILLIXR_BUILD_SUFFIX} ${Vulkan_DEP_STR})
    endif() 

    target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                               ${Boost_INCLUDE_DIR}
                               ${GLEW_INCLUDE_DIR}
                               ${GLU_INCLUDE_DIR}
                               ${gl_INCLUDE_DIRS}
    )

    target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                      stdc++fs
                      dl
                      ${GLEW_LIBRARIES}
                      ${glu_LDFLAGS}
                      ${gl_LIBRARIES}
                      ${Vulkan_LIBRARIES}
    )
endif() 

target_include_directories(main${ILLIXR_BUILD_SUFFIX} PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(main${ILLIXR_BUILD_SUFFIX} plugin.main${ILLIXR_BUILD_SUFFIX})


install(TARGETS main${ILLIXR_BUILD_SUFFIX} DESTINATION bin)
install(TARGETS plugin.main${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
