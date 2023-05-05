
add_library(plugin.offline_cam${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.offline_cam${ILLIXR_BUILD_SUFFIX} OPENCV_LIBRARY)
endif()
target_include_directories(plugin.offline_cam${ILLIXR_BUILD_SUFFIX} PRIVATE ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(plugin.offline_cam${ILLIXR_BUILD_SUFFIX} ${OpenCV_LIBRARIES})
target_compile_features(plugin.offline_cam${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)

install(TARGETS plugin.offline_cam${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
