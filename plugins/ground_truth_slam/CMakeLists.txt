add_library(plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX} OPENCV_LIBRARY)
    add_dependencies(plugin.ground_truth_slam OpenCV_Viz)
endif()

target_compile_features(plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)
target_include_directories(plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX} PRIVATE ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX} ${OpenCV_LIBRARIES})

install(TARGETS plugin.ground_truth_slam${ILLIXR_BUILD_SUFFIX} DESTINATION lib)

