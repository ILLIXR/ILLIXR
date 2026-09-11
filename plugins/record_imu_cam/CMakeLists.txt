# module to build and install the record_imu_cam ILLIXR plugin
if(ANDROID)
    message(WARNING "record_imu_cam is not yet available for Android")
    set(USE_RECORD_IMU_CAM OFF)
    return()
endif()

set(PLUGIN_NAME plugin.record_imu_cam${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
                           ${OpenCV_INCLUDE_DIRS}
                           ${ILLIXR_SOURCE_DIR}/include
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      opencv_core
                      opencv_imgproc
                      opencv_imgcodecs
                      Boost::filesystem
                      Boost::serialization
                      Eigen3::Eigen
                      spdlog::spdlog
)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
