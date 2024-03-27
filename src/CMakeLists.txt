add_executable(main${ILLIXR_BUILD_SUFFIX}.exe
               src/main.cpp
               src/illixr.hpp
               src/frame_logger2.hpp
               src/cxxopts.hpp
               include/illixr/frame_info.hpp
               include/illixr/phonebook.hpp
               include/illixr/runtime.hpp
               include/illixr/sqlite_wrapper.hpp
               include/illixr/switchboard.hpp
               include/illixr/cpu_timer/clock.hpp
               include/illixr/cpu_timer/cpu_timer.hpp
               include/illixr/cpu_timer/cpu_timer_internal.hpp
               include/illixr/cpu_timer/compiler_specific.hpp
               include/illixr/cpu_timer/global_state.hpp
               include/illixr/cpu_timer/os_specific.hpp
               include/illixr/cpu_timer/type_eraser.hpp
               include/illixr/cpu_timer/util.hpp
)
add_library(plugin.main${ILLIXR_BUILD_SUFFIX} SHARED
            src/plugin.cpp
            src/runtime_impl.cpp
            src/illixr.hpp
            src/cxxopts.hpp
            include/illixr/dynamic_lib.hpp
            include/illixr/error_util.hpp
            include/illixr/extended_window.hpp
            include/illixr/frame_info.hpp
            include/illixr/gen_guid.hpp
            include/illixr/global_module_defs.hpp
            include/illixr/managed_thread.hpp
            include/illixr/relative_clock.hpp
            include/illixr/runtime.hpp
            include/illixr/phonebook.hpp
            include/illixr/plugin.hpp
            include/illixr/sqlite_wrapper.hpp
            include/illixr/stoplight.hpp
            include/illixr/switchboard.hpp
            include/illixr/concurrentqueue/blockingconcurrentqueue.hpp
            include/illixr/concurrentqueue/concurrentqueue.hpp
            include/illixr/concurrentqueue/lightweightsemaphore.hpp
)

##############
# yaml parser
##############

get_external(Yamlcpp)

target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE "ILLIXR_VISUALIZERS='${VIS_NAMES}'")

target_include_directories(main${ILLIXR_BUILD_SUFFIX}.exe PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(main${ILLIXR_BUILD_SUFFIX}.exe boost_filesystem dl stdc++fs plugin.main${ILLIXR_BUILD_SUFFIX})

target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${SQLite3_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${yaml-cpp_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${SQLite3_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} dl Threads::Threads stdc++fs ${yaml-cpp_LIBRARIES} spdlog::spdlog)

install(TARGETS main${ILLIXR_BUILD_SUFFIX}.exe DESTINATION bin)
install(TARGETS plugin.main${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
