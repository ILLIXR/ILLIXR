cmake_minimum_required(VERSION 3.16)

project(openwarp_vk)

set(PLUGIN_NAME plugin.openwarp_vk${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external_for_plugin(Vulkan)
if(USE_MONADO)
    get_external_for_plugin(Monado_vk)
endif()
find_package(glm REQUIRED)

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
    message("${SPIRV}   ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}   ${GLSL}   ${FILE_NAME}")
endforeach (GLSL)

add_custom_target(
        OpenWarp_VK_Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        utils/hmd.cpp
        utils/hmd.hpp
        ../../include/illixr/global_module_defs.hpp
        ../../include/illixr/math_util.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/plugin.hpp
        ../../include/illixr/data_format/pose_prediction.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/vk/display_provider.hpp
        ../../include/illixr/vk/render_pass.hpp
        ../../include/illixr/vk/vulkan_utils.hpp
        )

add_dependencies(${PLUGIN_NAME} OpenWarp_VK_Shaders)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES} spdlog::spdlog)
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS})
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${ILLIXR_SOURCE_DIR}/include)

install_shaders(SPIRV_BINARY_FILES openwarp_vk ${PLUGIN_NAME})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
