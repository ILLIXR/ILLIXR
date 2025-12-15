set(PLUGIN_NAME plugin.common_lock${ILLIXR_BUILD_SUFFIX})
if (NOT ANDROID)
    message(FATAL_ERROR "${PLUGIN_NAME} is only intended for Android platforms.")
endif()

add_library(${PLUGIN_NAME} SHARED
        service.cpp
        service.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/common_lock.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
        ${OpenCV_LIBS}
        ${jnigraphics-lib}
        ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(${PLUGIN_NAME}
        EGL
        GLESv3
        Eigen3::Eigen
        spdlog::spdlog_android
        Boost::serialization
)
