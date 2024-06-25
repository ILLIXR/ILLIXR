# module to build and install the record_imu_cam ILLIXR plugin

set(PLUGIN_NAME plugin.record_imu_cam${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            ../../include/illixr/frame_info.hpp
            ../../include/illixr/managed_thread.hpp
            ../../include/illixr/opencv_data_types.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/plugin.hpp
            ../../include/illixr/sqlite_wrapper.hpp
            ../../include/illixr/switchboard.hpp
            ../../include/illixr/concurrentqueue/blockingconcurrentqueue.hpp
            ../../include/illixr/concurrentqueue/concurrentqueue.hpp
            ../../include/illixr/concurrentqueue/lightweightsemaphore.hpp
            ../../include/illixr/cpu_timer/clock.hpp
            ../../include/illixr/cpu_timer/cpu_timer.hpp
            ../../include/illixr/cpu_timer/cpu_timer_internal.hpp
            ../../include/illixr/cpu_timer/compiler_specific.hpp
            ../../include/illixr/cpu_timer/global_state.hpp
            ../../include/illixr/cpu_timer/os_specific.hpp
            ../../include/illixr/cpu_timer/type_eraser.hpp
            ../../include/illixr/cpu_timer/util.hpp
)

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${BOOST_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} boost_filesystem ${Eigen3_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
