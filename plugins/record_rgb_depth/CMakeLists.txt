
set(PLUGIN_NAME plugin.record_rgb_depth${ILLIXR_BUILD_SUFFIX})

set(RECORD_RGB_SOURCES plugin.cpp
    plugin.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
    ${CMAKE_SOURCE_DIR}/include/illixr/plugin.hpp
)
add_library(${PLUGIN_NAME} SHARED ${RECORD_RGB_SOURCES})

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} boost_filesystem)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
