# module to build and install the ILLIXR audio_input plugin

set(PLUGIN_NAME plugin.audio_input${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/openxr_data.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/openxr_payloads.h
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/openxr_payloads.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/utils/serialization/openxr_payloads.cpp
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      illixr_miniaudio
                      Boost::serialization
                      Eigen3::Eigen
)

if(ANDROID)
    target_include_directories(${PLUGIN_NAME} PRIVATE
                               ${ANDROID_NDK}/sources/android/native_app_glue
    )
    target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog_android)

else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
    if(WIN32 OR MSVC)
        target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
    endif()
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

if(NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
