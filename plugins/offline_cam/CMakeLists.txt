# module to build and install the offline_cam ILLIXR plugin
set(PLUGIN_NAME plugin.offline_cam${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED 
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_loading.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose_prediction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/iterators/csv_iterator.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/iterators/data_iterator.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE 
                           ${OpenCV_INCLUDE_DIRS} 
                           ${ILLIXR_SOURCE_DIR}/include
)

if(ANDROID)
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )
else()
    target_include_directories(${PLUGIN_NAME} PRIVATE 
                               ${Eigen3_INCLUDE_DIRS}
    )
endif()

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Boost::serialization
                      opencv_core
                      opencv_imgproc
                      opencv_imgcodecs
                      Eigen3::Eigen
)
if(ANDROID)
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          EGL
                          GLESv3
                          opencv_highgui
                          spdlog::spdlog_android
    )
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          spdlog::spdlog
    )
endif()


if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if(NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
