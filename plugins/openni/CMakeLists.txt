if(WIN32 OR MSVC)
    message(WARNING "openni is not yet available for windows")
    set(USE_OPENNI OFF)
else()
    pkg_check_modules(libopenni2 REQUIRED libopenni2)

    set(PLUGIN_NAME plugin.openni${ILLIXR_BUILD_SUFFIX})

    set(OPENNI_SOURCES plugin.cpp
        plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/phonebook.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
    )
    add_library(${PLUGIN_NAME} SHARED ${OPENNI_SOURCES})

    target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${libopenni2_INCLUDE_DIR})
    target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} ${libopenni2_LIBRARY} spdlog::spdlog)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
