# module to build and install the offload_vio.server_tx ILLIXR plugin

cmake_minimum_required(VERSION 3.16)
find_package(Protobuf REQUIRED)
include(../protoc_generate_cpp.cmake)

set(PLUGIN_NAME plugin.offload_vio.server_tx${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            ../../../include/illixr/data_format.hpp
            ../../../include/illixr/frame_info.hpp
            ../../../include/illixr/managed_thread.hpp
            ../../../include/illixr/phonebook.hpp
            ../../../include/illixr/relative_clock.hpp
            ../../../include/illixr/sqlite_wrapper.hpp
            ../../../include/illixr/switchboard.hpp

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
