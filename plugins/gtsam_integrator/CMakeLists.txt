# module to build and install the gtsam_integrator ILLIXR plugin
get_external_for_plugin(GTSAM)
set(PLUGIN_NAME plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            third_party/filter.h
            ../../include/illixr/data_format.hpp
            ../../include/illixr/frame_info.hpp
            ../../include/illixr/managed_thread.hpp
            ../../include/illixr/gen_guid.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/plugin.hpp
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

# if GTSAM is being built from source
if(GTSAM_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} ${GTSAM_DEP_STR})
endif()

#add_definitions(-Wall -Wextra -Werror)
target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIR} ${EIGEN3_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${GTSAM_LIBRARIES} ${Eigen3_LIBRARIES} spdlog::spdlog)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)

