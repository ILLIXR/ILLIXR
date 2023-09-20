# module to build and install the offload_vio.server_tx ILLIXR plugin

cmake_minimum_required(VERSION 3.16)
find_package(Protobuf REQUIRED)
include(../protoc_generate_cpp.cmake)

set(PLUGIN_NAME plugin.offload_vio.server_tx${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../../include/illixr/plugin.hpp
        ../../../include/illixr/data_format.hpp
        ../../../include/illixr/phonebook.hpp
        ../../../include/illixr/switchboard.hpp
        )

set(protobuf_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../proto/vio_output.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${Protobuf_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../proto ${protobuf_files})
target_link_libraries(${PLUGIN_NAME}
        protobuf::libprotobuf
        spdlog::spdlog
        )

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
