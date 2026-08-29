if(WIN32 OR MSVC)
    message(WARNING "openxr_quest_controller is currently available only on Linux/X11")
    set(USE_OPENXR_QUEST_CONTROLLER OFF)
else()
    # Conda-based ILLIXR builds may set CMAKE_PREFIX_PATH to the environment and
    # consequently miss the distro OpenXR loader package. Keep the normal search,
    # while also covering the standard Linux multiarch install locations.
    find_package(OpenXR CONFIG REQUIRED
                 PATHS
                 /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/openxr
                 /usr/local/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/openxr
                 /usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/cmake/openxr
                 /usr/local/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/cmake/openxr
                 /usr/lib/cmake/openxr
                 /usr/local/lib/cmake/openxr)
    find_path(OPENXR_HEADER_DIR
              NAMES openxr.h
              PATH_SUFFIXES openxr
              REQUIRED)

    set(PLUGIN_NAME plugin.openxr_quest_controller${ILLIXR_BUILD_SUFFIX})

    add_library(${PLUGIN_NAME} SHARED
                plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/openxr_view_frame.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/quest_controller.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/stereo_frame.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/stoplight.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${OPENXR_HEADER_DIR}
                               ${gl_INCLUDE_DIRS}
                               ${X11_INCLUDE_DIR}
    )
    target_link_libraries(${PLUGIN_NAME} PRIVATE
                          OpenXR::openxr_loader
                          Eigen3::Eigen
                          Threads::Threads
                          glfw
                          ${gl_LIBRARIES}
                          ${X11_LIBRARIES}
                          spdlog::spdlog
    )
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
