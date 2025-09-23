cmake_minimum_required(VERSION 3.22)

set(PLUGIN_NAME plugin.ada.device_tx${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/../proto/sr_input.proto)

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            video_encoder.cpp
            video_encoder.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../device_to_server_base.hpp
            ${PROTO_SRCS}
            ${PROTO_HDRS}
            ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/scene_reconstruction.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/network/net_config.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/plugin.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/stoplight.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/threadloop.hpp
            $<TARGET_OBJECTS:video_encoder_ada>
)

find_package(PkgConfig)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GST_AUDIO REQUIRED gstreamer-audio-1.0)

# gather the related include dirs
set(INC_DIRS ${GLIB_INCLUDE_DIRS} ${GSTREAMER_INCLUDE_DIRS} ${GST_APP_INCLUDE_DIRS} ${GST_AUDIO_INCLUDE_DIRS} ${GST_VIDEO_INCLUDE_DIRS})
# remove duplicates
list(REMOVE_DUPLICATES INC_DIRS)

# gather the libraries
set(LINK_LIBS ${GLIB_LIBRARIES} ${GSTREAMER_LIBRARIES} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES} ${GST_AUDIO_LIBRARIES})
# reverse to maintain order in final list
list(REVERSE LINK_LIBS)
# remove the copious duplicates
list(REMOVE_DUPLICATES LINK_LIBS)
# reverse back to original order
list(REVERSE LINK_LIBS)

target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_CURRENT_BINARY_DIR}
                           ${INC_DIRS}
                           ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      ${LINK_LIBS}
                      ${OpenCV_LIBS}
                      protobuf::libprotobuf
                      spdlog::spdlog
)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
