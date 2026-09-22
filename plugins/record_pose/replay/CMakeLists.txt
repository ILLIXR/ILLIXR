set(PLUGIN_NAME plugin.record_pose.replay${ILLIXR_BUILD_SUFFIX})

#if(USING_OPENXR)
find_package(OpenXR REQUIRED)

set(POSE_INJECTOR_FILES
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
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${POSE_INJECTOR_FILES}
)

target_include_directories(${PLUGIN_NAME} PRIVATE
                           ${ILLIXR_SOURCE_DIR}/include
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Boost::serialization
                      Eigen3::Eigen
)

if(ANDROID)
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${jnigraphics-lib}
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )

    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          spdlog::spdlog_android
                          OpenXR::openxr_loader
    )

    set(USE_POSE_INJECTOR ON PARENT_SCOPE)
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC
                          spdlog::spdlog
    )
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()
target_compile_definitions(${PLUGIN_NAME} PUBLIC USING_OPENXR)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if(NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
