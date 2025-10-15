# module to build and install the offload_vio.device_rx ILLIXR plugin

cmake_minimum_required(VERSION 3.22)

include(../protoc_generate_cpp.cmake)

set(PLUGIN_NAME plugin.tcp.device_tx${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED 
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

set(protobuf_files
        ${CMAKE_CURRENT_SOURCE_DIR}/../proto/input.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME}
        spdlog::spdlog
        protobuf::libprotobuf
        Boost::serialization
)
PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../proto ${protobuf_files})

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
