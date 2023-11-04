cmake_minimum_required(VERSION 3.16)

project(shared_memory_network_backend)

set(PLUGIN_NAME plugin.shared_memory_network_backend${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        )

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} spdlog::spdlog)
target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
