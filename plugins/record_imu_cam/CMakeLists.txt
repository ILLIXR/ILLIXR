if(NOT ${OpenCV_FOUND})
    find_package(OpenCV REQUIRED)
endif()

if(NOT ${Boost_FOUND})
    find_package(Boost REQUIRED COMPONENTS filesystem)
endif()

add_library(plugin.record_imu_cam SHARED plugin.cpp)
target_include_directories(plugin.record_imu_cam PRIVATE ${BOOST_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR})
target_link_libraries(plugin.record_imu_cam ${OpenCV_LIBRARIES} boost_filesystem)
target_compile_features(plugin.record_imu_cam PRIVATE cxx_std_17)

install(TARGETS plugin.record_imu_cam DESTINATION lib)
