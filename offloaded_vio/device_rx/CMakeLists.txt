cmake_minimum_required(VERSION 3.17)

project(plugin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(eCAL REQUIRED)
find_package(Protobuf REQUIRED)

add_library(plugin SHARED plugin.cpp)

set(protobuf_files
    ${CMAKE_CURRENT_SOURCE_DIR}/vio_output.proto
)

target_include_directories(plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

PROTOBUF_TARGET_CPP(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR} ${protobuf_files})

target_link_libraries(${PROJECT_NAME}
  eCAL::core
  protobuf::libprotobuf
)