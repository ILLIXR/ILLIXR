# module to build and install the fauxpose ILLIXR plugin

set(PLUGIN_NAME plugin.fauxpose${ILLIXR_BUILD_SUFFIX})

set(FAUXPOSE_SOURCES plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/pose_prediction.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
)

add_library(${PLUGIN_NAME} SHARED ${FAUXPOSE_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
