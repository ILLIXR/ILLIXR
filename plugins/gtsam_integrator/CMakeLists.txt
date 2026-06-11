# module to build and install the gtsam_integrator ILLIXR plugin
if(ANDROID)
    message(WARNING "gtsam_integrator is not yet available for Android")
    set(USE_GTSAM_INTEGRATOR OFF)
    return()
endif()

get_external_for_plugin(GTSAM)
set(PLUGIN_NAME plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED 
            plugin.cpp
            plugin.hpp
            third_party/filter.h
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

#?find_package(GTSAMCMakeTools REQUIRED)    Android

#add_definitions(-Wall -Wextra -Werror)
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} PUBLIC
                      gtsam
                      Eigen3::Eigen
                      spdlog::spdlog
                      Boost::serialization
)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
