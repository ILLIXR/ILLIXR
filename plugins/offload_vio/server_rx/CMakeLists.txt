cmake_minimum_required(VERSION 3.16)

set(PLUGIN_NAME plugin.offload_vio.server_rx${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

set(protobuf_files
    ${CMAKE_CURRENT_SOURCE_DIR}/vio_input.proto
)
PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR} ${protobuf_files})

include_directories(${OpenCV_INCLUDE_DIRS})
link_directories(${OpenCV_LIBRARY_DIRS})

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}  ${eCAL_INCLUDE_DIRS} ${Protobuf_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME}
  ${OpenCV_LIBS}
  eCAL::core
  protobuf::libprotobuf
)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
