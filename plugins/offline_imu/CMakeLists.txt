add_library(plugin.offline_imu SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.offline_imu OPENCV_LIBRARY)
endif()
target_compile_features(plugin.offline_imu PRIVATE cxx_std_17)
target_include_directories(plugin.offline_imu PRIVATE ${ILLIXR_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(plugin.offline_imu PRIVATE ${OpenCV_LIBRARIES})

install(TARGETS plugin.offline_imu DESTINATION lib)
