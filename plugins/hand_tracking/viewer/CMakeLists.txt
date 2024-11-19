

set(PLUGIN_NAME plugin.hand_tracking.viewer${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
        src/libs/data_format.cpp
        include/data_format.hpp
            ../../../include/illixr/hand_tracking_data.hpp
            ../../../include/illixr/threadloop.hpp
            ../../../include/illixr/switchboard.hpp
)

if(BUILD_OPENCV)
    add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBRARIES} boost_filesystem)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)