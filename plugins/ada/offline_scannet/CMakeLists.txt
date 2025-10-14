set(PLUGIN_NAME plugin.ada.offline_scannet${ILLIXR_BUILD_SUFFIX})
if(WIN32 OR MSVC)
    message(WARNING "ada.offline_scannet is not yet available for windows")
    set(USE_ADA.OFFLINE_SCANNET OFF)
else()

    # this is for the conversion script
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_library(${PLUGIN_NAME} SHARED
                plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_loading.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/scene_reconstruction.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/iterators/ssv_iterator.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/iterators/data_iterator.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
    )

    target_include_directories(${PLUGIN_NAME} PUBLIC ${${CMAKE_SOURCE_DIR}}/include ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBRARIES} spdlog::spdlog)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
