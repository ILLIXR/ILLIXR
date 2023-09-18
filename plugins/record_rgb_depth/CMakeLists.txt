
set(PLUGIN_NAME plugin.record_rgb_depth${ILLIXR_BUILD_SUFFIX})

set(RECORD_RGB_SOURCES plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/plugin.hpp
        )
add_library(${PLUGIN_NAME} SHARED ${RECORD_RGB_SOURCES})

# OpenCV
# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} boost_filesystem)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
