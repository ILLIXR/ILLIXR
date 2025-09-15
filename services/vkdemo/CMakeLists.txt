cmake_minimum_required(VERSION 3.22)

project(vkdemo)

set(PLUGIN_NAME plugin.vkdemo${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


file(GLOB_RECURSE GLSL_SOURCE_FILES
        "shaders/*.frag"
        "shaders/*.vert"
        )

set(VKDEMO_DEP_STR "")
if(Vulkan_EXTERNAL)
    set(VKDEMO_DEP_STR ${Vulkan_DEP_STR})
endif()

foreach (GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
            COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} ${GLSL} -V -o ${SPIRV}
            DEPENDS ${GLSL} ${VKDEMO_DEP_STR})
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach (GLSL)

add_custom_target(
        Vkdemo_Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_library(${PLUGIN_NAME} SHARED service.cpp
            service.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/global_module_defs.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/math_util.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/gl_util/lib/tiny_obj_loader.h
            ${CMAKE_SOURCE_DIR}/include/illixr/gl_util/lib/stb_image.h
            $<TARGET_OBJECTS:illixr_vulkan_utils>
)

add_dependencies(${PLUGIN_NAME} Vkdemo_Shaders)
set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
target_link_libraries(${PLUGIN_NAME} PUBLIC 
        spdlog::spdlog
        Boost::serialization
        Eigen3::Eigen
)
target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include)

if(WIN32 OR MSVC)
    target_link_libraries(${PLUGIN_NAME} PUBLIC 
            Vulkan::Vulkan
            GPUOpen::VulkanMemoryAllocator
    )
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
else()
    if(Vulkan_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
    endif()

    target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES})
    target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS})
endif()

install_shaders(SPIRV_BINARY_FILES vkdemo ${PLUGIN_NAME})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
