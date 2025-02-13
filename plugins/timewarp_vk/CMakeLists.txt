cmake_minimum_required(VERSION 3.16)

project(timewarp_vk)

set(PLUGIN_NAME plugin.timewarp_vk${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external_for_plugin(Vulkan)
find_package(glm REQUIRED)

file(GLOB_RECURSE GLSL_SOURCE_FILES
        "shaders/*.frag"
        "shaders/*.vert"
        )
set(TIMEWARP_VK_DEP_STR "")
if(Vulkan_EXTERNAL)
    set(TIMEWARP_VK_DEP_STR ${Vulkan_DEP_STR})
endif()

foreach (GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_target(PROCESS_TWV_${FILE_NAME} ALL
                      COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
                      COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} ${GLSL} -V -o ${SPIRV}
                      BYPRODUCTS ${SPIRV}
                      DEPENDS ${GLSL} ${TIMEWARP_VK_DEP_STR}
    )
    if(Vulkan_EXTERNAL)
        add_dependencies(PROCESS_TWV_${FILE_NAME} ${Vulkan_DEP_STR})
    endif()
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach (GLSL)
add_custom_target(
        Timewarp_VK_Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            $<TARGET_OBJECTS:illixr_hmd>
            ../../include/illixr/data_format.hpp
            ../../include/illixr/global_module_defs.hpp
            ../../include/illixr/math_util.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/plugin.hpp
            ../../include/illixr/pose_prediction.hpp
            ../../include/illixr/switchboard.hpp
            ../../include/illixr/vk/display_provider.hpp
            ../../include/illixr/vk/render_pass.hpp
            $<TARGET_OBJECTS:illixr_vulkan_utils>
)

if(ENABLE_MONADO)
    get_external_for_plugin(Monado_vk)
    target_compile_definitions(${PLUGIN_NAME} PUBLIC ENABLE_MONADO)
endif()

add_dependencies(${PLUGIN_NAME} Timewarp_VK_Shaders)
if(Vulkan_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
endif()

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES} spdlog::spdlog)
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS})
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${ILLIXR_SOURCE_DIR}/include)

install_shaders(SPIRV_BINARY_FILES timewarp_vk ${PLUGIN_NAME})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
