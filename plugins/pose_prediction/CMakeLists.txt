# CMake file for building pose_prediction plugin for ILLIXR
# Required packages:
#   OpenCV including the viz module  (development)

add_library(plugin.pose_prediction SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.pose_prediction OPENCV_LIBRARY)
endif()
target_compile_features(plugin.pose_prediction PRIVATE cxx_std_17)
target_include_directories(plugin.pose_prediction PRIVATE ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.pose_prediction ${OpenCV_LIBRARIES})

install(TARGETS plugin.pose_prediction DESTINATION lib)
