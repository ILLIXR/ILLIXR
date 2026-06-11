if(ANDROID)
    message(WARNING "record_depth_rgb is not yet available for Android")
    set(USE_RECORD_DEPTH_RGB OFF)
    return()
endif()

set(PLUGIN_NAME plugin.record_rgb_depth${ILLIXR_BUILD_SUFFIX})

set(RECORD_RGB_SOURCES
    plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
)
add_library(${PLUGIN_NAME} SHARED ${RECORD_RGB_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      opencv_core
                      opencv_imgproc
                      opencv_imgcodecs
                      Boost::filesystem
                      Boost::serialization
                      spdlog::spdlog
)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
