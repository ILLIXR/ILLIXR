# module to build and install the depthai ILLIXR plugin

get_external_for_plugin(DepthAI)

set(PLUGIN_NAME plugin.depthai${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            ../../include/illixr/data_format.hpp
            ../../include/illixr/error_util.hpp
            ../../include/illixr/frame_info.hpp
            ../../include/illixr/gen_guid.hpp
            ../../include/illixr/global_module_defs.hpp
            ../../include/illixr/managed_thread.hpp
            ../../include/illixr/opencv_data_types.hpp
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
add_definitions(-Wno-format-extra-args)

# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${DepthAI_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

# if we are building DepthAI from source
if(DepthAI_EXTERNAL)
    add_dependencies(${PLUGIN_NAME} DepthAI_ext)
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE ${DepthAI_LIBRARIES} spdlog::spdlog)
else()  # DepthAI already installed
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE depthai::core)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
