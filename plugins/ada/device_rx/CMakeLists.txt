cmake_minimum_required(VERSION 3.22)

set(PLUGIN_NAME plugin.ada.device_rx${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf REQUIRED)

get_external(Draco)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/../proto/sr_output.proto)
add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            ${PROTO_SRCS}
            ${PROTO_HDRS}
            ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/mesh.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/scene_reconstruction.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${ILLIXR_SOURCE_DIR}/include/illixr/threadloop.hpp
)
target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_CURRENT_BINARY_DIR}
                           ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME}
                      protobuf::libprotobuf
                      draco_illixr::draco
                      ${OpenCV_LIBS}
)
if(TARGET draco_static)
    add_dependencies(${PLUGIN_NAME} draco_static)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${draco_illixr_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
