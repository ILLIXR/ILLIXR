cmake_minimum_required(VERSION 3.22)

project(timewarp_vk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glm REQUIRED)

if(NOT PLUGIN_NAME)
    set(PLUGIN_NAME "plugin.timewarp_vk${ILLIXR_BUILD_SUFFIX}")
    set(PLUGIN_SOURCE "plugin.cpp")
    set(TW_PREFIX "")
    set(TW_POSTFIX "")
endif()

file(GLOB_RECURSE GLSL_SOURCE_FILES
     "${TW_PREFIX}shaders/*.frag"
     "${TW_PREFIX}shaders/*.vert"
)
set(TIMEWARP_VK_DEP_STR "")
if(Vulkan_EXTERNAL)
    set(TIMEWARP_VK_DEP_STR ${Vulkan_DEP_STR})
endif()

set(PROJECT_BINARY_DIR "${PROJECT_BINARY_DIR}${TW_POSTFIX}")

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    if(NOT TARGET PROCESS_TWV_${FILE_NAME})
        add_custom_target(PROCESS_TWV_${FILE_NAME} ALL
                          COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
                          COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} ${GLSL} -V -o ${SPIRV}
                          BYPRODUCTS ${SPIRV}
                          DEPENDS ${GLSL} ${TIMEWARP_VK_DEP_STR}
        )
        if(Vulkan_EXTERNAL)
            add_dependencies(PROCESS_TWV_${FILE_NAME} ${Vulkan_DEP_STR})
        endif()
    endif()
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)
if(NOT TARGET Timewarp_VK_Shaders)
    add_custom_target(
            Timewarp_VK_Shaders
            DEPENDS ${SPIRV_BINARY_FILES}
    )
endif()

add_library(${PLUGIN_NAME} SHARED
            ${PLUGIN_SOURCE}
            ${TW_PREFIX}plugin.hpp
            ${TW_PREFIX}timewarp_vk.cpp
            ${TW_PREFIX}timewarp_vk.hpp
            $<TARGET_OBJECTS:illixr_hmd>
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
            $<TARGET_OBJECTS:illixr_vulkan_utils>
)
if(TW_PLUGIN_DEFINITIONS)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE ${TW_PLUGIN_DEFINITIONS})
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
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
