# module to build and install the rk4_integrator ILLIXR plugin

set(PLUGIN_NAME plugin.rk4_integrator${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED 
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} 
        spdlog::spdlog
        Boost::serialization
        Eigen3::Eigen
)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
