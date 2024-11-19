set(PLUGIN_NAME plugin.webcam${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
        src/libs/data_format.cpp
        include/data_format.hpp
            ../../../include/illixr/hand_tracking_data.hpp
            ../../../include/illixr/opencv_data_types.hpp
            ../../../include/illixr/threadloop.hpp
            ../../../include/illixr/relative_clock.hpp
            ../../../include/illixr/switchboard.hpp
)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PUBLIC ${OpenCV_INCLUDE_DIRS} /home/friedel/Downloads/devel/ILLIXR/plugins/webcam/include ${BOOST_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBRARIES} boost_filesystem)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)