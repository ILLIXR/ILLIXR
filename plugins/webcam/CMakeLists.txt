set(PLUGIN_NAME plugin.webcam${ILLIXR_BUILD_SUFFIX})

add_library(${PLUGIN_NAME} SHARED plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/relative_clock.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
)

target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_SOURCE_DIR}/include
                           ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Eigen3::Eigen
                      Boost::serialization
                      spdlog::spdlog
                      opencv_core
                      opencv_imgproc
                      opencv_videoio
)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
