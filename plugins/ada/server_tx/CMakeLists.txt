cmake_minimum_required(VERSION 3.17)
if(WIN32 OR MSVC)
    message(WARNING "ada.server_tx is not yet available for windows")
    set(USE_ADA.SERVER_TX OFF)
else()

    set(PLUGIN_NAME plugin.ada.server_tx${ILLIXR_BUILD_SUFFIX})

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    get_external(Draco)
    find_package(Protobuf REQUIRED)
    include_directories(${CMAKE_CURRENT_BINARY_DIR})
    PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/../proto/sr_output.proto)

    add_library(${PLUGIN_NAME} SHARED
                plugin.hpp
                plugin.cpp
                ${PROTO_SRCS}
                ${PROTO_HDRS}
                ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/mesh.hpp
                ${ILLIXR_SOURCE_DIR}/include/illixr/data_format/scene_reconstruction.hpp
                ${ILLIXR_SOURCE_DIR}/include/illixr/phonebook.hpp
                ${ILLIXR_SOURCE_DIR}/include/illixr/plugin.hpp
                ${ILLIXR_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    target_include_directories(${PLUGIN_NAME} PUBLIC
                               ${OpenCV_INCLUDE_DIRS}
                               ${ILLIXR_SOURCE_DIR}/include
    )

    target_link_libraries(${PLUGIN_NAME}
                          ${OpenCV_LIBS}
                          protobuf::libprotobuf
                          draco_illixr::draco
    )

    if(TARGET draco_static)
        add_dependencies(${PLUGIN_NAME} draco_static)
        target_include_directories(${PLUGIN_NAME} PUBLIC ${draco_illixr_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
    endif()

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
