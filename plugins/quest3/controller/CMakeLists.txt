if(NOT ANDROID)
    message(WARNING "quest3.controller is currently available only on Android")
    set(USE_OPENXR_QUEST_CONTROLLER OFF)
    return()
endif()
# Conda-based ILLIXR builds may set CMAKE_PREFIX_PATH to the environment and
# consequently miss the distro OpenXR loader package.
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

set(PLUGIN_NAME plugin.quest3.controller${ILLIXR_BUILD_SUFFIX})

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

# Match the Boba producer's pose layout for shared view and stereo_frame events.
target_compile_definitions(${PLUGIN_NAME} PRIVATE USING_OPENXR)
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
                      spdlog::spdlog_android
)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
