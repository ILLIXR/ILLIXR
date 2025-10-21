cmake_minimum_required(VERSION 3.16)
if(WIN32 OR MSVC)
    message(WARNING "offload_rendering_server is not yet available for windows")
    set(USE_OFFLOAD_RENDERING_SERVER OFF)
else()

    project(offload_rendering_server)

    # Note that the offload rendering server only supports Monado (not native)
    get_external_for_plugin(Monado_vk)
    set(PLUGIN_NAME "plugin.offload_rendering_server${ILLIXR_BUILD_SUFFIX}")
    set(PLUGIN_SOURCE "${CMAKE_BINARY_DIR}/plugins/offload_rendering_server/plugin.cpp")

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # ffmpeg
    get_external_for_plugin(ILLIXR_FFmpeg)

    add_compile_definitions(ILLIXR_LIBAV)

    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                offload_rendering_server.cpp
                offload_rendering_server.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serializable_data.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/frame.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/display_provider.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/ffmpeg_utils.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/render_pass.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_utils.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
                ${CMAKE_SOURCE_DIR}/utils/serializable_data.cpp
    )

    target_compile_definitions(${PLUGIN_NAME} PRIVATE MONADO_REQUIRED)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
    if(TARGET ILLIXR_FFmpeg_ext)
        add_dependencies(${PLUGIN_NAME} ILLIXR_FFmpeg_ext)
        message(WARNING "ADD DEP serv")
    endif()

    target_link_libraries(${PLUGIN_NAME} illixr_vulkan_utils spdlog::spdlog ${Vulkan_LIBRARIES})
    target_include_directories(${PLUGIN_NAME} BEFORE PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

    target_link_libraries(${PLUGIN_NAME} ${libavdevice_illixr_LIBRARIES} ${libavformat_illixr_LIBRARIES} ${libavcodec_illixr_LIBRARIES} ${libavutil_illixr_LIBRARIES})
    target_include_directories(${PLUGIN_NAME} PUBLIC ${libavcodec_illixr_INCLUDE_DIRS})
    get_target_property(INC_DIRS ${PLUGIN_NAME} INCLUDE_DIRECTORIES)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
