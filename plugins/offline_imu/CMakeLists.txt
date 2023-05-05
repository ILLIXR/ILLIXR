add_library(plugin.offline_imu${ILLIXR_BUILD_SUFFIX} SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.offline_imu${ILLIXR_BUILD_SUFFIX} OpenCV_Viz)
endif()
target_compile_features(plugin.offline_imu${ILLIXR_BUILD_SUFFIX} PRIVATE cxx_std_17)
target_include_directories(plugin.offline_imu${ILLIXR_BUILD_SUFFIX} PRIVATE ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.offline_imu${ILLIXR_BUILD_SUFFIX} PRIVATE ${OpenCV_LIBRARIES})

install(TARGETS plugin.offline_imu${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
