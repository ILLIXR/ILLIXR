find_package(Protobuf REQUIRED)
include(../protoc_generate_cpp.cmake)

set(PLUGIN_NAME plugin.quest3.head_pose.rx${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
)

set(protobuf_files
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto/quest_pose.proto
)

target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           {Protobuf_INCLUDE_DIRS}
)

PROTOBUF_TARGET_CPP(${PLUGIN_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../proto ${protobuf_files})

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      protobuf::libprotobuf
                      spdlog::spdlog
                      Boost::serialization
)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
    target_compile_options(${PLUGIN_NAME} PRIVATE /EHsc)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
