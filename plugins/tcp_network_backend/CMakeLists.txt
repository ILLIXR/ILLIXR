cmake_minimum_required(VERSION 3.16)

project(tcp_network_backend)

set(PLUGIN_NAME plugin.tcp_network_backend${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/network/network_backend.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/network/tcpsocket.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/network/topic_config.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

if (ANDROID)
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          spdlog::spdlog_android
                          Boost::serialization
                          Eigen3::Eigen
    )
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
endif()

target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include)

if (NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
