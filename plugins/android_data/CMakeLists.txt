set(PLUGIN_NAME plugin.android_data${ILLIXR_BUILD_SUFFIX})
if (NOT ANDROID)
    message(FATAL_ERROR "${PLUGIN_NAME} is only intended for Android platforms.")
endif()
add_library(${PLUGIN_NAME} SHARED
        plugin.cpp
        plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE
        ${OpenCV_LIBS}
        ${jnigraphics-lib}
        ${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(${PLUGIN_NAME}
        EGL
        GLESv3
        opencv_core
        opencv_imgproc
        opencv_highgui
        opencv_imgcodecs
        camera2ndk
        mediandk
        android
        Eigen3::Eigen
        spdlog::spdlog_android
)
