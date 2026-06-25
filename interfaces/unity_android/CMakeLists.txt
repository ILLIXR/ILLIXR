set(ILLIXR_INTERFACE_NAME unity_android_interface${ILLIXR_BUILD_SUFFIX})

add_library(${ILLIXR_INTERFACE_NAME} SHARED
            unity_bridge_jni.cpp
            unity_component.cpp
            unity_component.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/semantics.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/point_cloud.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/point_cloud_ser.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/point_cloud_ser.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/voice_query.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/voice_query_ser.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/voice_query_ser.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/query_response.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/query_response_ser.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/query_response_ser.cpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${ILLIXR_INTERFACE_NAME} PRIVATE
                           ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(${ILLIXR_INTERFACE_NAME} PRIVATE
                           ILLIXR_BUILD_SUFFIX_STR="${ILLIXR_BUILD_SUFFIX}"
)

target_link_libraries(${ILLIXR_INTERFACE_NAME} PRIVATE
                      plugin.main${ILLIXR_BUILD_SUFFIX}
                      android
                      spdlog::spdlog_android
                      Eigen3::Eigen
                      Boost::serialization
)
