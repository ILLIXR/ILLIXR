cmake_minimum_required(VERSION 3.16)

set(PLUGIN_NAME plugin.offload_vio.server_tx${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/plugin.hpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        )

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

set(protobuf_files
    ${CMAKE_CURRENT_SOURCE_DIR}/vio_output.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS} ${eCAL_INCLUDE_DIRS} ${Protobuf_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

if(eCAL_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} eCAL_ext)
    include(../protoc_generate_cpp.cmake)
    PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR} ${protobuf_files})
    target_link_libraries(${PLUGIN_NAME}
            ${OpenCV_LIBS}
            ${eCAL_LIBRARIES}
            protobuf::libprotobuf
            )

else()
    PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR} ${protobuf_files})
    target_link_libraries(${PLUGIN_NAME}
            ${OpenCV_LIBS}
            eCAL::core
            protobuf::libprotobuf
            )
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
