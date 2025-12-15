# module to build and install the fauxpose ILLIXR service

set(PLUGIN_NAME plugin.fauxpose${ILLIXR_BUILD_SUFFIX})

set(FAUXPOSE_SOURCES service.cpp
    service.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
)

add_library(${PLUGIN_NAME} SHARED ${FAUXPOSE_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)

if (ANDROID)
target_include_directories(${PLUGIN_NAME} PRIVATE
        ${jnigraphics-lib}
        ${ANDROID_NDK}/sources/android/native_app_glue
)
endif()

target_link_libraries(${PLUGIN_NAME} PUBLIC Eigen3::Eigen)
if (ANDROID)
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          EGL
                          GLESv3
                          Eigen3::Eigen
                          spdlog::spdlog_android
    )
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if (NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
