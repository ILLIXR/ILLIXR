# module to build and install the fauxpose ILLIXR plugin

set(PLUGIN_NAME plugin.fauxpose${ILLIXR_BUILD_SUFFIX})

set(FAUXPOSE_SOURCES plugin.cpp
        ../../include/illixr/plugin.hpp
        ../../include/illixr/pose_prediction.hpp
        )

add_library(${PLUGIN_NAME} SHARED ${FAUXPOSE_SOURCES})
# if building on CentOS make sure we use the correct OpenCV
if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE ${ILLIXR_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES})
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
