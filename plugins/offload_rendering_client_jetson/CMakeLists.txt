cmake_minimum_required(VERSION 3.16)
if (ON_JETSON_PLATFORM)
    project(offload_rendering_client_jetson)

    set(PLUGIN_NAME plugin.offload_rendering_client_jetson${ILLIXR_BUILD_SUFFIX})

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    get_external_for_plugin(Vulkan)

    include_directories(/usr/src/jetson_multimedia_api/include)
    include_directories(/usr/src/jetson_multimedia_api/include/libjpeg-8b)

    # add *.cpp from /usr/src/jetson_multimedia_api/samples/common/classes
    file(GLOB_RECURSE JETSON_MULTIMEDIA_API_CLASSES /usr/src/jetson_multimedia_api/samples/common/classes/*.cpp)

    # remove NvVulkanRenderer.cpp from the list
    list(REMOVE_ITEM JETSON_MULTIMEDIA_API_CLASSES /usr/src/jetson_multimedia_api/samples/common/classes/NvVulkanRenderer.cpp)

    link_directories(/usr/lib/aarch64-linux-gnu/tegra)

    find_package (Threads)

    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                offload_rendering_client_jetson.cpp
                offload_rendering_client_jetson.hpp
                ../../include/illixr/data_format.hpp
                ../../include/illixr/phonebook.hpp
                ../../include/illixr/switchboard.hpp
                ../../include/illixr/threadloop.hpp
                ../../include/illixr/vk/display_provider.hpp
                $<TARGET_OBJECTS:illixr_vulkan_utils>
                $<TARGET_OBJECTS:illixr_serializable_data>
                ${JETSON_MULTIMEDIA_API_CLASSES}
                decoding/video_decode.h
                decoding/video_decode.cpp
                #        decoding/NvVulkanRenderer.cpp
    )

    target_link_libraries(${PLUGIN_NAME} drm)
    target_link_libraries(${PLUGIN_NAME} nvjpeg)
    target_link_libraries(${PLUGIN_NAME} nvv4l2)
    target_link_libraries(${PLUGIN_NAME} EGL)
    target_link_libraries(${PLUGIN_NAME} GLESv2)
    target_link_libraries(${PLUGIN_NAME} X11)
    target_link_libraries(${PLUGIN_NAME} nvbufsurface)
    target_link_libraries(${PLUGIN_NAME} nvbufsurftransform)
    target_link_libraries(${PLUGIN_NAME} nvosd)
    target_link_libraries(${PLUGIN_NAME} ${CMAKE_THREAD_LIBS_INIT})
    target_link_libraries(${PLUGIN_NAME} ${Vulkan_LIBRARIES})
    target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} /usr/include/libdrm)

    set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
    if(Vulkan_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
    endif()

    target_link_libraries(${PLUGIN_NAME} spdlog::spdlog ${Vulkan_LIBRARIES})
    target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)



    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
else()
    set(USE_OFFLOAD_RENDERING_CLIENT_JETSON OFF PARENT_SCOPE)
endif()
