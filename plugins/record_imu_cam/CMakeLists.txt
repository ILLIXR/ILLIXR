# module to build and install the record_imu_cam ILLIXR plugin

set(PLUGIN_NAME plugin.record_imu_cam${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${BOOST_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} boost_filesystem Eigen3::Eigen)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
