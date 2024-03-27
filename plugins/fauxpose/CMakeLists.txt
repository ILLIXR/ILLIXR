# module to build and install the fauxpose ILLIXR plugin

set(PLUGIN_NAME plugin.fauxpose${ILLIXR_BUILD_SUFFIX})

set(FAUXPOSE_SOURCES plugin.cpp
    ../../include/illixr/data_format.hpp
    ../../include/illixr/frame_info.hpp
    ../../include/illixr/gen_guid.hpp
    ../../include/illixr/managed_thread.hpp
    ../../include/illixr/phonebook.hpp
    ../../include/illixr/plugin.hpp
    ../../include/illixr/pose_prediction.hpp
    ../../include/illixr/relative_clock.hpp
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

add_library(${PLUGIN_NAME} SHARED ${FAUXPOSE_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)
target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
