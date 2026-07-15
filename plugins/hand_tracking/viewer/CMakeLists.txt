if(WIN32 OR MSVC)
    message(WARNING "hand_tracking.viewer is not yet available for windows")
    set(USE_HAND_TRACKING_VIEWER OFF)
else()

    set(PLUGIN_NAME plugin.hand_tracking.viewer${ILLIXR_BUILD_SUFFIX})

    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/hand_tracking_data.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                $<TARGET_OBJECTS:illixr_imgui>
    )

    if(BUILD_OPENCV)
        add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
    endif()
    #target_compile_definitions(${PLUGIN_NAME} PUBLIC -DVIEW_DUMP)
    target_include_directories(${PLUGIN_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include ${OpenCV_INCLUDE_DIRS} ${Eigen3_INCLUDE_DIRS})
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBRARIES} ${Eigen3_LIBRARIES} dl Threads::Threads ${GLEW_LIBRARIES})
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
