if(NOT ANDROID)
    message(WARNING "The common_lock plugin is only for Android platforms")
    return()
endif()
set(PLUGIN_NAME plugin.common_lock${ILLIXR_BUILD_SUFFIX})

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
                           ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(${PLUGIN_NAME}
                      EGL
                      GLESv3
                      Eigen3::Eigen
                      spdlog::spdlog_android
                      Boost::serialization
)
