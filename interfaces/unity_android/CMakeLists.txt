add_library(unity_android_interface SHARED
            unity_bridge_jni.cpp
            unity_component.cpp
            unity_component.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/semantics.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(unity_android_interface PRIVATE
                           ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(unity_android_interface PRIVATE
                      plugin.main${ILLIXR_BUILD_SUFFIX}
                      android
                      spdlog::spdlog_android
                      Eigen3::Eigen
                      Boost::serialization
)
