# CMakeLists.txt for network_latency/rx plugin
# Network latency measurement server (receiver/responder)

set(PLUGIN_NAME plugin.network_latency.rx${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED
        plugin.hpp
        plugin.cpp
        ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/latency_data.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/misc.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization/latency.hpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/misc.cpp
        ${CMAKE_SOURCE_DIR}/utils/serialization/latency.cpp
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} PUBLIC
        Boost::serialization
        Eigen3::Eigen
)

if(ANDROID)
    target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog_android)
    target_include_directories(${PLUGIN_NAME} PUBLIC
            ${ANDROID_NDK}/sources/android/native_app_glue
    )
else()
    target_link_libraries(${PLUGIN_NAME} PUBLIC spdlog::spdlog)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

# Platform-specific configuration
if(WIN32)
    target_link_libraries(${PLUGIN_NAME} PUBLIC ws2_32)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
            BUILDING_LIBRARY
            _WIN32_WINNT=0x0601
            WIN32_LEAN_AND_MEAN
    )
endif()

if(NOT ANDROID)
    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()