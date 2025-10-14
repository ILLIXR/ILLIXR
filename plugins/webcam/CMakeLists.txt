if(WIN32 OR MSVC)
    message(WARNING "webcam is not yet available for windows")
    set(USE_WEBCAM OFF)
else()
    set(PLUGIN_NAME plugin.webcam${ILLIXR_BUILD_SUFFIX})

    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
    )

    if(BUILD_OPENCV)
        add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBRARIES})

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
