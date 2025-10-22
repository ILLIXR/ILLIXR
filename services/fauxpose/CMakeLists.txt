# module to build and install the fauxpose ILLIXR service
set(PLUGIN_NAME plugin.fauxpose${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED
    service.cpp
    service.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} PUBLIC 
        spdlog::spdlog
        Boost::serialization
)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
