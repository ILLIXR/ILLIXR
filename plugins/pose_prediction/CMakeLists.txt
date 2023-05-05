# CMake file for building pose_prediction plugin for ILLIXR
# Required packages:
#   OpenCV including the viz module  (development)

add_library(plugin.pose_prediction${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.pose_prediction${ILLIXR_BUILD_SUFFIX} OpenCV_Viz)
endif()
target_compile_features(plugin.pose_prediction${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)
target_include_directories(plugin.pose_prediction${ILLIXR_BUILD_SUFFIX} PRIVATE ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.pose_prediction${ILLIXR_BUILD_SUFFIX} ${OpenCV_LIBRARIES})

install(TARGETS plugin.pose_prediction${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
