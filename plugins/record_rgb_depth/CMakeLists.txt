
set(PLUGIN_NAME plugin.record_rgb_depth${ILLIXR_BUILD_SUFFIX})

set(RECORD_RGB_SOURCES plugin.cpp
    ../../include/illixr/frame_info.hpp
    ../../include/illixr/managed_thread.hpp
    ../../include/illixr/phonebook.hpp
    ../../include/illixr/plugin.hpp
    ../../include/illixr/sqlite_wrapper.hpp
    ../../include/illixr/switchboard.hpp
    ../../include/illixr/opencv_data_types.hpp
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

add_library(${PLUGIN_NAME} SHARED ${RECORD_RGB_SOURCES})

# OpenCV
# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} boost_filesystem)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
