cmake_minimum_required(VERSION 3.16)

project(display_vk)

set(PLUGIN_NAME plugin.display_vk${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(FullVulkan REQUIRED)
if(USE_MONADO)
    get_external_for_plugin(Monado_vk)
endif()

find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)

# X11
find_package(X11 REQUIRED)

# Wayland
find_package(PkgConfig REQUIRED)
pkg_search_module(LIBDRM REQUIRED IMPORTED_TARGET libdrm)

find_package(Wayland REQUIRED Client)
find_package(WaylandProtocols REQUIRED)
find_package(WaylandScanner REQUIRED)

file(GLOB_RECURSE GLSL_SOURCE_FILES
        "shaders/*.frag"
        "shaders/*.vert"
        )

foreach (GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
            COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} ${GLSL} -V -o ${SPIRV}
            DEPENDS ${GLSL})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach (GLSL)

add_custom_target(
        Display_VK_Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_compile_definitions(VK_USE_PLATFORM_XLIB_KHR)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        third_party/VkBootstrap.cpp
        ../../include/illixr/vk_util/third_party/VkBootstrap.h
        ../../include/illixr/vk_util/third_party/VkBootstrapDispatch.h
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/vk_util/display_sink.hpp
        ../../include/illixr/vk_util/vulkan_utils.hpp
        wayland_drm.h
        wayland_drm.cpp
        display_backend.h
        x11_direct.cpp
        x11_direct.h
        glfw_extended.cpp
        glfw_extended.h
)

add_dependencies(${PLUGIN_NAME} Display_VK_Shaders)
set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} glfw)
target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES} spdlog::spdlog)
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${LIBDRM_INCLUDE_DIRS})

# X11
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${X11_INCLUDE_DIR})

# Wayland
target_link_libraries(${PLUGIN_NAME} PkgConfig::LIBDRM)

pkg_get_variable(WAYLAND_PROTOCOLS_PKGDATADIR wayland-protocols pkgdatadir)
ecm_add_wayland_client_protocol(WAYLAND_HEADERS PROTOCOL ${WAYLAND_PROTOCOLS_PKGDATADIR}/staging/drm-lease/drm-lease-v1.xml BASENAME drm-lease-v1)
MESSAGE(STATUS "WAYLAND_HEADERS: ${WAYLAND_HEADERS}")
add_library(${PLUGIN_NAME}_WAYLAND STATIC ${WAYLAND_HEADERS})
target_include_directories(${PLUGIN_NAME}_WAYLAND PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PLUGIN_NAME} Wayland::Client ${PLUGIN_NAME}_WAYLAND)

add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/shaders/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/shaders"
        )

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
