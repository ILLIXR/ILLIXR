# module to build and install the pose_prediction ILLIXR plugin

set(PLUGIN_NAME plugin.pose_prediction${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED 
            service.cpp
            service.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include)

if(ANDROID)
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )
endif()

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Eigen3::Eigen
                      Boost::serialization
)

if(ANDROID)
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          EGL
                          GLESv3
                          log
                          spdlog::spdlog_android
    )
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          spdlog::spdlog
    )
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

if(NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
