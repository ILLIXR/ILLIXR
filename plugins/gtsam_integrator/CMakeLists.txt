if(NOT Eigen3_FOUND)
    find_package(Eigen3 REQUIRED)
endif()

add_library(plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)
if(NOT GTSAM_FOUND)
    add_dependencies(plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX} gtsam)
endif()

if(BUILD_OPENCV)
    add_dependencies(plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX} OPENCV_LIBRARY)
    add_dependencies(plugin.gtsam_integrator OpenCV_Viz)
endif()


#add_definitions(-Wall -Wextra -Werror)
target_include_directories(plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIR} ${EIGEN3_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX} PRIVATE ${GTSAM_LIBRARIES} ${OpenCV_LIBRARIES})

install(TARGETS plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX} DESTINATION lib)

