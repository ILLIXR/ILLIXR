cmake_minimum_required(VERSION 3.17)

project(plugin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

find_package(eCAL REQUIRED)
find_package(Protobuf REQUIRED)
find_package(OpenCV REQUIRED)

add_library(plugin SHARED plugin.cpp)

set(protobuf_files
    ${CMAKE_CURRENT_SOURCE_DIR}/vio_input.proto
)
PROTOBUF_TARGET_CPP(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR} ${protobuf_files})

include_directories(${OpenCV_INCLUDE_DIRS})
link_directories(${OpenCV_LIBRARY_DIRS})

target_include_directories(plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME}
  ${OpenCV_LIBS}
  eCAL::core
  protobuf::libprotobuf
)