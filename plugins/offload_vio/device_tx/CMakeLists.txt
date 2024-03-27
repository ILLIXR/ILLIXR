# module to build and install the offload_vio.device_tx ILLIXR plugin

cmake_minimum_required(VERSION 3.16)
find_package(Protobuf REQUIRED)
include(../protoc_generate_cpp.cmake)

set(PLUGIN_NAME plugin.offload_vio.device_tx${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            video_encoder.cpp
            video_encoder.h
            ../../../include/illixr/data_format.hpp
            ../../../include/illixr/error_util.hpp
            ../../../include/illixr/frame_info.hpp
            ../../../include/illixr/gen_guid.hpp
            ../../../include/illixr/global_module_defs.hpp
            ../../../include/illixr/managed_thread.hpp
            ../../../include/illixr/opencv_data_types.hpp
            ../../../include/illixr/phonebook.hpp
            ../../../include/illixr/plugin.hpp
            ../../../include/illixr/relative_clock.hpp
            ../../../include/illixr/sqlite_wrapper.hpp
            ../../../include/illixr/stoplight.hpp
            ../../../include/illixr/switchboard.hpp
            ../../../include/illixr/threadloop.hpp
            ../../../include/illixr/concurrentqueue/blockingconcurrentqueue.hpp
            ../../../include/illixr/concurrentqueue/concurrentqueue.hpp
            ../../../include/illixr/concurrentqueue/lightweightsemaphore.hpp
            ../../../include/illixr/cpu_timer/clock.hpp
            ../../../include/illixr/cpu_timer/cpu_timer.hpp
            ../../../include/illixr/cpu_timer/cpu_timer_internal.hpp
            ../../../include/illixr/cpu_timer/compiler_specific.hpp
            ../../../include/illixr/cpu_timer/global_state.hpp
            ../../../include/illixr/cpu_timer/os_specific.hpp
            ../../../include/illixr/cpu_timer/type_eraser.hpp
            ../../../include/illixr/cpu_timer/util.hpp
            ../../../include/illixr/network/net_config.hpp
            ../../../include/illixr/network/socket.hpp
            ../../../include/illixr/network/address.hpp
            ../../../include/illixr/network/exception.hpp
            ../../../include/illixr/network/file_descriptor.hpp
            ../../../include/illixr/network/timestamp.hpp
            ../../../include/illixr/network/ezio.hpp
            ../../../include/illixr/network/util.hpp
)

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

set(protobuf_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../proto/vio_input.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS} ${Protobuf_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../proto ${protobuf_files})
target_link_libraries(${PLUGIN_NAME}
        ${OpenCV_LIBS}
        protobuf::libprotobuf
        )

find_package(PkgConfig)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GST_AUDIO REQUIRED gstreamer-audio-1.0)

include_directories(
        ${GSTREAMER_INCLUDE_DIRS}
        ${GLIB_INCLUDE_DIRS}
)

target_link_libraries(${PLUGIN_NAME} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES} ${GST_AUDIO_LIBRARIES} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)

get_cmake_property(_variableNames VARIABLES)
foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()
