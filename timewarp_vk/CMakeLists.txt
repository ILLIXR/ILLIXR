cmake_minimum_required(VERSION 3.17)

project(timewarp_vk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan)

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
endforeach (GLSL)

add_custom_target(
        Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_library(timewarp_vk SHARED plugin.cpp utils/hmd.cpp)
add_dependencies(timewarp_vk Shaders)
set_target_properties(timewarp_vk PROPERTIES OUTPUT_NAME "plugin")

target_link_libraries(timewarp_vk ${Vulkan_LIBRARIES})
target_include_directories(timewarp_vk PUBLIC ${Vulkan_INCLUDE_DIRS})
target_include_directories(timewarp_vk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(TARGET timewarp_vk POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:timewarp_vk>/shaders/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:timewarp_vk>/shaders"
        )