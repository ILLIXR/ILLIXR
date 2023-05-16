cmake_minimum_required(VERSION 3.16)

set(PLUGIN_NAME plugin.offload_vio.server_tx${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp)

set(protobuf_files
    ${CMAKE_CURRENT_SOURCE_DIR}/vio_output.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${eCAL_INCLUDE_DIRS} ${Protobuf_INCLUDE_DIRS})

PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR} ${protobuf_files})

target_link_libraries(${PLUGIN_NAME}
  eCAL::core
  protobuf::libprotobuf
)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
