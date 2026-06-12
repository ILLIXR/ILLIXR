if(NOT ANDROID)
    add_compile_definitions(VK_USE_PLATFORM_XLIB_KHR)

    if(WIN32 OR MSVC)
        set(EXE_NAME main${ILLIXR_BUILD_SUFFIX})
    else()
        set(EXE_NAME main${ILLIXR_BUILD_SUFFIX}.exe)
    endif()

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

    add_executable(${EXE_NAME}
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
                src/vulkan_display.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
                ${VULKAN_FILES}
                $<TARGET_OBJECTS:illixr_vulkan_utils>
    )

    if(WIN32 OR MSVC)
        target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE BUILDING_LIBRARY)
        target_compile_options(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE /EHsc)
        target_compile_options(${EXE_NAME} PRIVATE /EHsc)
    endif()

    if(BUILD_OXR_INTERFACE)
        target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC -DOXR_INTERFACE)
    endif()

    get_external(Yamlcpp)

    target_compile_definitions(plugin.main${ILLIXR_BUILD_SUFFIX} PRIVATE ILLIXR_INSTALL_PATH="${CMAKE_INSTALL_PREFIX}")

    target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                               ${SQLite3_INCLUDE_DIR}
                               ${yaml-cpp_INCLUDE_DIRS}
                               #${X11_INCLUDE_DIR}
                               ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include
                               ${CMAKE_SOURCE_DIR}/include
    )
    target_link_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                            ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib
    )
    target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                          Threads::Threads
                          yaml-cpp::yaml-cpp
                          spdlog::spdlog
                          ${SQLite3_LIBRARIES}
                          Boost::serialization
                          X11
                          glfw
    )

    if(WIN32 OR MSVC)
        target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                              GLEW::GLEW
                              OpenGL::GL
                              Vulkan::Vulkan
                              GPUOpen::VulkanMemoryAllocator
        )
    else()

        if(Vulkan_DEP_STR)
            add_dependencies(plugin.main${ILLIXR_BUILD_SUFFIX} ${Vulkan_DEP_STR})
        endif()

        target_include_directories(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                                   ${Boost_INCLUDE_DIR}
                                   ${GLEW_INCLUDE_DIR}
                                   ${GLU_INCLUDE_DIR}
                                   ${gl_INCLUDE_DIRS}
        )

        target_link_libraries(plugin.main${ILLIXR_BUILD_SUFFIX} PUBLIC
                              stdc++fs
                              dl
                              ${GLEW_LIBRARIES}
                              ${glu_LDFLAGS}
                              ${gl_LIBRARIES}
                              ${Vulkan_LIBRARIES}
        )
    endif()

    target_include_directories(${EXE_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${EXE_NAME} plugin.main${ILLIXR_BUILD_SUFFIX})

    install(TARGETS ${EXE_NAME} DESTINATION bin)
    install(TARGETS plugin.main${ILLIXR_BUILD_SUFFIX} DESTINATION lib)
else()
    find_package(yaml-cpp_android REQUIRED CONFIG)

    add_library(native-activity SHARED
                src/illixr.hpp
                src/main.cpp
                src/plugin.cpp
                src/runtime_impl.cpp
                src/no_op_record_logger.hpp
                src/sqlite_record_logger.hpp
                src/stdout_record_logger.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/dynamic_lib.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/error_util.hpp
                #${CMAKE_SOURCE_DIR}/include/illixr/extended_window.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/runtime.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/stoplight.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                src/android/network_config_dialog.cpp
                src/android/network_config_dialog.hpp
                src/android/profile_picker_dialog.cpp
                src/android/profile_picker_dialog.hpp
    )
    target_include_directories(native-activity PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/android
                               ${ILLIXR_SOURCE_DIR}/include
                               ${OpenCV_LIBS}
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )

    # ANDROID_PLUGIN_LIST is built by the plugin loop in the top-level
    # CMakeLists.txt: every plugin whose USE_<name> flag is ON after read_yaml()
    # is appended to this list.  src/CMakeLists.txt is included after that loop,
    # so the list is guaranteed to be populated by the time we get here.
    if(NOT ANDROID_PLUGIN_LIST)
        message(FATAL_ERROR
            "ANDROID_PLUGIN_LIST is empty.  Ensure a YAML_FILE is specified and "
            "contains an enabled plugins: list before the top-level CMakeLists.txt "
            "processes the plugin loop.")
    endif()

    # Link each plugin library derived from the profile into the runtime.
    # The target names follow the same convention used on Linux/Windows:
    #   plugin.<name>${ILLIXR_BUILD_SUFFIX}
    foreach(ANDROID_PLUGIN IN LISTS ANDROID_PLUGIN_LIST)
        target_link_libraries(native-activity
                              plugin.${ANDROID_PLUGIN}${ILLIXR_BUILD_SUFFIX}
        )
    endforeach()

    target_link_libraries(native-activity
                          android
                          native_app_glue
                          EGL
                          GLESv3
                          Eigen3::Eigen
                          Boost::serialization
                          spdlog::spdlog_android
                          yaml-cpp::yaml-cpp_android
    )

    # Copy profile YAML files into the Gradle project's assets directory so
    # they are bundled into the APK.  Two sources are combined:
    #
    #   1. Profiles generated from plugins.yaml by generate_yaml(), identified
    #      by ILLIXR_PROFILE_NAMES.  These are always in sync with the current
    #      build configuration because both generation and this copy step run
    #      at CMake configure time.
    #
    #   2. Any additional *.yaml files present in profiles/ that were created
    #      manually by the user outside of plugins.yaml.  These are discovered
    #      via file(GLOB ...) and copied alongside the generated profiles,
    #      allowing users to maintain personal configurations without modifying
    #      the repository.
    #
    # Files from both sources are de-duplicated: a hand-created file with the
    # same name as a generated one will not be double-copied (the generated
    # version takes precedence since it is processed first).
    set(ANDROID_ASSETS_PROFILES_DIR
        "${CMAKE_SOURCE_DIR}/app/src/main/assets/profiles")
    file(MAKE_DIRECTORY "${ANDROID_ASSETS_PROFILES_DIR}")

    # Track which filenames have already been copied to detect duplicates.
    set(_copied_profiles "")

    # 1. Generated profiles (authoritative list from generate_yaml())
    foreach(PROFILE_NAME IN LISTS ILLIXR_PROFILE_NAMES)
        configure_file(
            "${CMAKE_SOURCE_DIR}/profiles/${PROFILE_NAME}.yaml"
            "${ANDROID_ASSETS_PROFILES_DIR}/${PROFILE_NAME}.yaml"
            COPYONLY)
        list(APPEND _copied_profiles "${PROFILE_NAME}.yaml")
    endforeach()

    # 2. User-created profiles not already covered above
    file(GLOB _all_profile_files "${CMAKE_SOURCE_DIR}/profiles/*.yaml")
    foreach(_profile_file IN LISTS _all_profile_files)
        get_filename_component(_profile_filename "${_profile_file}" NAME)
        list(FIND _copied_profiles "${_profile_filename}" _already_copied)
        if(_already_copied EQUAL -1)
            configure_file("${_profile_file}"
                           "${ANDROID_ASSETS_PROFILES_DIR}/${_profile_filename}"
                           COPYONLY)
            list(APPEND _copied_profiles "${_profile_filename}")
        endif()
    endforeach()

    if(NOT _copied_profiles)
        message(FATAL_ERROR
            "No profile YAML files found in ${CMAKE_SOURCE_DIR}/profiles/. "
            "At least one profile is required for the Android build.")
    endif()
    message(STATUS "Android APK profiles: ${_copied_profiles}")

endif()