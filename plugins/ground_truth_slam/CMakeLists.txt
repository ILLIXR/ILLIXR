add_library(plugin.ground_truth_slam SHARED plugin.cpp)
target_compile_features(plugin.ground_truth_slam PRIVATE cxx_std_17)
target_include_directories(plugin.ground_truth_slam PRIVATE ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.ground_truth_slam ${OpenCV_LIBRARIES})

install(TARGETS plugin.ground_truth_slam DESTINATION lib)

