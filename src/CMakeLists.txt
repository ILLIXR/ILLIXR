if (NOT ANDROID)
    find_package(glfw3 3.3 REQUIRED)

    add_compile_definitions(VK_USE_PLATFORM_XLIB_KHR)

    set(VULKAN_FILES
        src/display/x11_direct.hpp
        src/display/x11_direct.cpp
        src/display/glfw_extended.hpp
        src/display/glfw_extended.cpp
        src/display/headless.hpp
        src/display/headless.cpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/vk/vulkan_objects.hpp
    )

    add_executable(main${ILLIXR_BUILD_SUFFIX}.exe
                   src/main.cpp
                   src/illixr.hpp
                   src/cxxopts.hpp
                   ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
                   ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    )

    add_library(plugin.main${ILLIXR_BUILD_SUFFIX} SHARED
                src/plugin.cpp
                src/runtime_impl.cpp
                src/illixr.hpp
                src/cxxopts.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
                ${VULKAN_FILES}
    )

##############
# yaml parser
##############
    if(BUILD_OXR_INTERFACE)
        target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC -DOXR_INTERFACE)
    endif()
    message("plugin.main${ILLIXR_BUILD_SUFFIX}")
    get_external(Yamlcpp)

    target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE "ILLIXR_VISUALIZERS='${VIS_NAMES}'")

    if(Vulkan_DEP_STR)
        add_dependencies(plugin.main${ILLIXR_BUILD_SUFFIX} ${Vulkan_DEP_STR})
    endif()

    target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE ILLIXR_INSTALL_PATH="${CMAKE_INSTALL_PREFIX}")
    target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                               ${Boost_INCLUDE_DIR}
                               ${X11_INCLUDE_DIR}
                               ${GLEW_INCLUDE_DIR}
                               ${SQLite3_INCLUDE_DIR}
                               ${GLU_INCLUDE_DIR}
                               ${gl_INCLUDE_DIRS}
                               ${yaml-cpp_INCLUDE_DIRS}
                               ${CMAKE_SOURCE_DIR}/include
                               ${Vulkan_INCLUDE_DIRS}
    )

    target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                          ${X11_LIBRARIES}
                          ${GLEW_LIBRARIES}
                          ${SQLite3_LIBRARIES}
                          ${glu_LDFLAGS}
                          ${gl_LIBRARIES}
                          dl
                          Threads::Threads
                          stdc++fs
                          ${yaml-cpp_LIBRARIES}
                          spdlog::spdlog
                          Boost::serialization
                          illixr_vulkan_utils
                          glfw
                          ${Vulkan_LIBRARIES}
    )

    target_include_directories(main${ILLIXR_BUILD_SUFFIX}.exe PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(main${ILLIXR_BUILD_SUFFIX}.exe dl stdc++fs plugin.main${ILLIXR_BUILD_SUFFIX})


    install(TARGETS main${ILLIXR_BUILD_SUFFIX}.exe DESTINATION bin)
    install(TARGETS plugin.main${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
else()
    add_library(native-activity SHARED
                illixr.hpp
                main.cpp
                plugin.cpp
                runtime_impl.cpp
                noop_record_logger.hpp
                sqlite_record_logger.hpp
                stdout_record_logger.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/dynamic_lib.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/extended_window.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/stoplight.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    target_include_directories(native-activity PRIVATE
                               ${ILLIXR_SOURCE_DIR}/include
                               ${OpenCV_LIBS}
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )

    target_link_libraries(native-activity
                          plugin.extended_window${ILLIXR_BUILD_SUFFIX}
                          OpenVINS_plugin
                          plugin.pose_prediction${ILLIXR_BUILD_SUFFIX}
                          plugin.pose_lookup${ILLIXR_BUILD_SUFFIX}
                          plugin.fauxpose${ILLIXR_BUILD_SUFFIX}
                          plugin.common_lock${ILLIXR_BUILD_SUFFIX}
                          plugin.timewarp_gl${ILLIXR_BUILD_SUFFIX}
                          plugin.rk4_integrator${ILLIXR_BUILD_SUFFIX}
                          plugin.offline_imu${ILLIXR_BUILD_SUFFIX}
                          plugin.offline_cam${ILLIXR_BUILD_SUFFIX}
                          #plugin.gtsam_integrator${ILLIXR_BUILD_SUFFIX}
                          plugin.gldemo${ILLIXR_BUILD_SUFFIX}
                          plugin.android_data${ILLIXR_BUILD_SUFFIX}
                          plugin.tcp_network_backend${ILLIXR_BUILD_SUFFIX}
                          android
                          native_app_glue
                          EGL
                          GLESv3
                          Eigen3::Eigen
                          Boost::serialization
                          spdlog::spdlog_android
    )
endif()
