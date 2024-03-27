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

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            third_party/VkBootstrap.cpp
            ../../include/illixr/data_format.hpp
            ../../include/illixr/error_util.hpp
            ../../include/illixr/frame_info.hpp
            ../../include/illixr/gen_guid.hpp
            ../../include/illixr/global_module_defs.hpp
            ../../include/illixr/managed_thread.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/plugin.hpp
            ../../include/illixr/relative_clock.hpp
            ../../include/illixr/sqlite_wrapper.hpp
            ../../include/illixr/stoplight.hpp
            ../../include/illixr/switchboard.hpp
            ../../include/illixr/threadloop.hpp
            ../../include/illixr/concurrentqueue/blockingconcurrentqueue.hpp
            ../../include/illixr/concurrentqueue/concurrentqueue.hpp
            ../../include/illixr/concurrentqueue/lightweightsemaphore.hpp
            ../../include/illixr/cpu_timer/clock.hpp
            ../../include/illixr/cpu_timer/cpu_timer.hpp
            ../../include/illixr/cpu_timer/cpu_timer_internal.hpp
            ../../include/illixr/cpu_timer/compiler_specific.hpp
            ../../include/illixr/cpu_timer/global_state.hpp
            ../../include/illixr/cpu_timer/os_specific.hpp
            ../../include/illixr/cpu_timer/type_eraser.hpp
            ../../include/illixr/cpu_timer/util.hpp
            ../../include/illixr/vk_util/display_sink.hpp
            ../../include/illixr/vk_util/vulkan_utils.hpp
            ../../include/illixr/vk_util/third_party/VkBootstrap.h
            ../../include/illixr/vk_util/third_party/VkBootstrapDispatch.h
            ../../include/illixr/vk_util/third_party/vk_mem_alloc.h
        )

add_dependencies(${PLUGIN_NAME} Display_VK_Shaders)
set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} glfw)
target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES} spdlog::spdlog)
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/shaders/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_BINARY_DIR}/shaders"
        "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/shaders"
        )

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
