# module to build and install the offline_imu ILLIXR plugin

set(PLUGIN_NAME plugin.offline_imu${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            data_loading.hpp
            ../../include/illixr/csv_iterator.hpp
            ../../include/illixr/data_format.hpp
            ../../include/illixr/error_util.hpp
            ../../include/illixr/frame_info.hpp
            ../../include/illixr/gen_guid.hpp
            ../../include/illixr/global_module_defs.hpp
            ../../include/illixr/managed_thread.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/plugin.hpp
            ../../include/illixr/relative_clock.hpp
            ../../include/illixr/sqlite_wrapper.hpp
            ../../include/illixr/stoplight.hpp
            ../../include/illixr/switchboard.hpp
            ../../include/illixr/threadloop.hpp
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

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${Eigne3_LIBRARIES} spdlog::spdlog)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
