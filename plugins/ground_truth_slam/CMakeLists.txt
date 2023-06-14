# module to build and install the ground_truth_slam ILLIXR plugin

set(PLUGIN_NAME plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        csv_iterator.hpp
        data_loading.hpp
        ../../include/illixr/plugin.hpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        )

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS} ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} ${Eigen3_LIBRARIES})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)

