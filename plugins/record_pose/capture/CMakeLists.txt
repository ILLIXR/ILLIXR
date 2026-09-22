set(PLUGIN_NAME plugin.record_pose.capture${ILLIXR_BUILD_SUFFIX})

#if(USING_OPENXR)
find_package(OpenXR REQUIRED)

set(POSE_CAPTURE_FILES
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses/combined_pose.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/combined_pose.hpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/combined_pose.cpp
    ${CMAKE_SOURCE_DIR}/utils/serialization/head_pose.cpp
)
#else()
#    set(POSE_CAPTURE_FILES
#
#    )
#endif()

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${POSE_CAPTURE_FILES}
)

target_include_directories(${PLUGIN_NAME} PRIVATE
                           ${ILLIXR_SOURCE_DIR}/include
                           ${jnigraphics-lib}
                           ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(${PLUGIN_NAME}
                      Boost::serialization
                      Eigen3::Eigen
                      spdlog::spdlog_android
                      OpenXR::openxr_loader
)

if(ANDROID)
    set(USE_POSE_CAPTURE ON PARENT_SCOPE)
endif()
target_compile_definitions(${PLUGIN_NAME} PUBLIC USING_OPENXR BOOST_NOT_TRACK_POSE)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
