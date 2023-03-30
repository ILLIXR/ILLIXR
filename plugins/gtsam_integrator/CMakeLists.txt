if(NOT ${Eigen3_FOUND})
    find_package(Eigen3 REQUIRED)
endif()

add_library(plugin.gtsam_integrator SHARED plugin.cpp)

#add_definitions(-Wall -Wextra -Werror)
target_include_directories(plugin.gtsam_integrator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIRS} ${EIGEN3_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.gtsam_integrator PRIVATE gtsam ${OpenCV_LIBRARIES})

install(TARGETS plugin.gtsam_integrator DESTINATION lib)

