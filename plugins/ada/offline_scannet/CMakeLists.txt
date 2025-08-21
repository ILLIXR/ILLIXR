set(PLUGIN_NAME plugin.offline_scannet${ILLIXR_BUILD_SUFFIX})

# this is for the conversion script
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(${PLUGIN_NAME}
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_loading.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/scene_reconstruction.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${${CMAKE_SOURCE_DIR}}/include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PUBLIC ${OpenCV_LIBRARIES} spdlog::spdlog)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
