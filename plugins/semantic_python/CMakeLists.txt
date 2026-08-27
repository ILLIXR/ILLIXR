set(PLUGIN_NAME plugin.semantic_python${ILLIXR_BUILD_SUFFIX})

# make sure the correct path for pybind11 is followed
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.27.0")
    cmake_minimum_required(VERSION 3.27)
endif()

find_package(pybind11 CONFIG REQUIRED)

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            switchboard_bindings.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/point_cloud.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/query_response.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/semantics.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/voice_query.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/point_cloud.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/query_response.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/semantics.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/voice_query.hpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/point_cloud.cpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/query_response.cpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/semantics.cpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/voice_query.cpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
                           ${ILLIXR_SOURCE_DIR}/include
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      pybind11::embed
                      Eigen3::Eigen
                      Boost::serialization
                      spdlog::spdlog
)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
