# module to build and install the offload_vio.server_rx ILLIXR plugin

cmake_minimum_required(VERSION 3.16)
find_package(Protobuf REQUIRED)
include(../protoc_generate_cpp.cmake)

set(PLUGIN_NAME plugin.offload_vio.server_rx${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        video_decoder.cpp
        video_decoder.h
        ../../../include/illixr/plugin.hpp
        ../../../include/illixr/data_format.hpp
        ../../../include/illixr/opencv_data_types.hpp
        ../../../include/illixr/phonebook.hpp
        ../../../include/illixr/switchboard.hpp
        )

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

set(protobuf_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../proto/vio_input.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS}  ${Protobuf_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../proto ${protobuf_files})
target_link_libraries(${PLUGIN_NAME}
        ${OpenCV_LIBS}
        protobuf::libprotobuf
        spdlog::spdlog
        )

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

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

target_link_libraries(${PLUGIN_NAME} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES} ${GST_AUDIO_LIBRARIES})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
