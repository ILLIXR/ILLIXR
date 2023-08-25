# module to build and install the gtsam_integrator ILLIXR plugin

set(PLUGIN_NAME plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/plugin.hpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/switchboard.hpp
        )

# if GTSAM is being built from source
if(NOT GTSAM_FOUND)
    add_dependencies(${PLUGIN_NAME} ${GTSAM_DEP_STR})
endif()

#add_definitions(-Wall -Wextra -Werror)
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIR} ${EIGEN3_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${GTSAM_LIBRARIES} ${Eigen3_LIBRARIES})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)

