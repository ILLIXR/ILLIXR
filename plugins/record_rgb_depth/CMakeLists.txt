if(WIN32 OR MSVC)
    message(WARNING "record_depth_rgb is not yet available for windows")
    set(USE_RECORD_DEPTH_RGB OFF)
else()

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
endif()
