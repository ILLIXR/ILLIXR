# module to build and install the gtsam_integrator ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "gtsam_integrator is not yet available for windows")
    set(USE_GTSAM_INTEGRATOR OFF)
else()

    get_external_for_plugin(GTSAM)
    set(PLUGIN_NAME plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX})

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    # if GTSAM is being built from source
    if(GTSAM_EXTERNAL)
        add_dependencies(${PLUGIN_NAME} ${GTSAM_DEP_STR})
    endif()

    #add_definitions(-Wall -Wextra -Werror)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR}/include)
    target_link_libraries(${PLUGIN_NAME} ${GTSAM_LIBRARIES} Eigen3::Eigen spdlog::spdlog)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
