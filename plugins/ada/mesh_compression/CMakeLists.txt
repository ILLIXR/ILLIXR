cmake_minimum_required(VERSION 3.22)

set(PLUGIN_NAME plugin.ada.mesh_compression${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external(Draco)

add_library(${PLUGIN_NAME} SHARED
            plugin.hpp
            plugin.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/mesh.hpp
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_INSTALL_PREFIX}/include)
target_link_libraries(${PLUGIN_NAME} PUBLIC draco_illixr::draco spdlog::spdlog)

if(TARGET draco_static)
    add_dependencies(${PLUGIN_NAME} draco_static)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${draco_illixr_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
