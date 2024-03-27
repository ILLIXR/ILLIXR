# module to build and install the gldemo ILLIXR plugin

set(PLUGIN_NAME plugin.gldemo${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
            ../../include/illixr/data_format.hpp
            ../../include/illixr/frame_info.hpp
            ../../include/illixr/error_util.hpp
            ../../include/illixr/extended_window.hpp
            ../../include/illixr/gen_guid.hpp
            ../../include/illixr/global_module_defs.hpp
            ../../include/illixr/managed_thread.hpp
            ../../include/illixr/math_util.hpp
            ../../include/illixr/phonebook.hpp
            ../../include/illixr/plugin.hpp
            ../../include/illixr/pose_prediction.hpp
            ../../include/illixr/relative_clock.hpp
            ../../include/illixr/shader_util.hpp
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
            ../../include/illixr/gl_util/obj.hpp
            ../../include/illixr/gl_util/lib/stb_image.h
            ../../include/illixr/gl_util/lib/tiny_obj_loader.h
            ../../include/illixr/shaders/demo_shader.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES}  ${Eigen3_LIBRARIES} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
