cmake_minimum_required(VERSION 3.16)

project(shared_memory_network_backend)

set(PLUGIN_NAME plugin.shared_memory_network_backend${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external_for_plugin(ILLIXR_FFmpeg)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/serializable_data.hpp
        ../../include/illixr/serializable_data.cpp
)
if(ILLIXR_FFmpeg_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${ILLIXR_FFmpeg_DEP_STR})
endif()

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} spdlog::spdlog Boost::boost)
target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include ${Boost_INCLUDE_DIR})

target_link_libraries(${PLUGIN_NAME} ${libavdevice_illixr_LIBRARIES} ${libavformat_illixr_LIBRARIES} ${libavcodec_illixr_LIBRARIES} ${libavutil_illixr_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${libavcodec_illixr_INCLUDE_DIRS})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
