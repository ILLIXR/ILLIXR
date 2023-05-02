
add_library(plugin.offline_cam SHARED plugin.cpp)

if(BUILD_OPENCV)
    add_dependencies(plugin.offline_cam OPENCV_LIBRARY)
endif()
target_include_directories(plugin.offline_cam PRIVATE ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(plugin.offline_cam ${OpenCV_LIBRARIES})
target_compile_features(plugin.offline_cam PRIVATE cxx_std_17)

install(TARGETS plugin.offline_cam DESTINATION lib)
