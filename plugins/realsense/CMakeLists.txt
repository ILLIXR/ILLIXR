# module to build and install the realsense ILLIXR plugin
if(WIN32 OR MSVC)
    message(WARNING "realsense is not yet available for windows")
    set(USE_REALSENSE OFF)
else()

    set(PLUGIN_NAME plugin.realsense${ILLIXR_BUILD_SUFFIX})

    find_package(realsense2 REQUIRED)
    find_package(realsense2-gl REQUIRED)

    # source files, listed individually so that any changes will trigger a rebuild
    add_library(${PLUGIN_NAME} SHARED plugin.cpp
                plugin.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/misc.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/switchboard.hpp
                ${CMAKE_SOURCE_DIR}/include/illixr/threadloop.hpp
    )

    target_include_directories(${PLUGIN_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS} ${Eigen3_INCLUDE_DIRS} ${realsense2_INCLUDE_DIR} ${ILLIXR_SOURCE_DIR}/include)
    add_definitions(-Wno-format-extra-args)
    target_link_libraries(${PLUGIN_NAME} ${DEPENDENCIES} ${OpenCV_LIBRARIES} ${realsense2_LIBRARY} ${Eigen3_LIBRATIES} spdlog::spdlog)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
