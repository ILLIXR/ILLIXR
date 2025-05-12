cmake_minimum_required(VERSION 3.16)

project(openwarp_vk)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glm REQUIRED)

if(NOT PLUGIN_NAME)
    set(PLUGIN_NAME "plugin.openwarp_vk${ILLIXR_BUILD_SUFFIX}")
    set(PLUGIN_SOURCE "plugin.cpp")
    set(OW_PREFIX "")
    set(OW_POSTFIX "")
endif()

file(GLOB_RECURSE GLSL_SOURCE_FILES
     "${OW_PREFIX}shaders/*.frag"
     "${OW_PREFIX}shaders/*.vert"
)

set(PROJECT_BINARY_DIR "${PROJECT_BINARY_DIR}${OW_POSTFIX}")

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    if(NOT TARGET PROCESS_OW_${FILE_NAME})
        add_custom_target(PROCESS_OW_${FILE_NAME} ALL
                          COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
                          COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} ${GLSL} -V -o ${SPIRV}
                          BYPRODUCTS ${SPIRV}
                          DEPENDS ${GLSL}
        )
        if(Vulkan_EXTERNAL)
            add_dependencies(PROCESS_OW_${FILE_NAME} ${Vulkan_DEP_STR})
        endif()
    endif()
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

if(NOT TARGET OpenWarp_VK_Shaders)
    add_custom_target(
            OpenWarp_VK_Shaders
            DEPENDS ${SPIRV_BINARY_FILES}
    )
endif()

add_library(${PLUGIN_NAME} SHARED
            ${PLUGIN_SOURCE}
            ${OW_PREFIX}plugin.hpp
            ${OW_PREFIX}openwarp_vk.hpp
            ${OW_PREFIX}openwarp_vk.cpp
            $<TARGET_OBJECTS:illixr_hmd>
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
            $<TARGET_OBJECTS:illixr_vulkan_utils>
)
if(OW_PLUGIN_DEFINITIONS)
    target_compile_definitions(${PLUGIN_NAME} PUBLIC ${OW_PLUGIN_DEFINITIONS})
    target_compile_definitions(${PLUGIN_NAME} PRIVATE MONADO_REQUIRED)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

add_dependencies(${PLUGIN_NAME} OpenWarp_VK_Shaders)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} PUBLIC ${Vulkan_LIBRARIES} spdlog::spdlog)
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS})
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${ILLIXR_SOURCE_DIR}/include)

install_shaders(SPIRV_BINARY_FILES openwarp_vk ${PLUGIN_NAME})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
