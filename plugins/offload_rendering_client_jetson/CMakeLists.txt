cmake_minimum_required(VERSION 3.16)
if (ON_JETSON_PLATFORM)
    project(offload_rendering_client_jetson)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(libdrm REQUIRED libdrm)
    pkg_check_modules(GLESv2 REQUIRED glesv2)

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

    find_package(Threads)

    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                offload_rendering_client_jetson.cpp
                offload_rendering_client_jetson.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serializable_data.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
                ${CMAKE_SOURCE_DIR}/utils/serializable_data.cpp
                ${JETSON_MULTIMEDIA_API_CLASSES}
                decoding/video_decode.h
                decoding/video_decode.cpp
                #        decoding/NvVulkanRenderer.cpp
    )

    target_link_libraries(${PLUGIN_NAME}
                          illixr_vulkan_utils
                          ${libdrm_LIBRARIES}
                          nvjpeg
                          nvv4l2
                          ${GLESv2_LIBRARIES}
                          ${X11_LIBRARIES}
                          nvbufsurface
                          nvbufsurftransform
                          nvosd
                          ${CMAKE_THREAD_LIBS_INIT}
                          ${Vulkan_LIBRARIES}
                          spdlog::spdlog
    )
    target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC
                               ${Vulkan_INCLUDE_DIRS}
                               ${libdrm_INCLUDE_DIRS}
                               ${ILLIXR_SOURCE_DIR}/include
                               ${X11_INCLUDE_DIR}
                               ${EGL_INCLUDE_DIRS}
                               ${GLESv2_INCLUDE_DIRS}
    )

    set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
    if(Vulkan_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} ${Vulkan_DEP_STR})
    endif()


    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
else()
    set(USE_OFFLOAD_RENDERING_CLIENT_JETSON OFF PARENT_SCOPE)
endif()
