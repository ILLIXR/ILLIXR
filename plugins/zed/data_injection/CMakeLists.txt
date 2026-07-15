if(WIN32 OR MSVC)
    message(WARNING "zed.data_injection is not yet available for windows")
    set(USE_ZED.DATA_INJECTION OFF)
else()
    # To add compiler flags for debugging, uncomment the lines below.
    # SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    # SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

    CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

    set(PLUGIN_NAME plugin.zed.data_injection${ILLIXR_BUILD_SUFFIX})

    set(SRC_FILES plugin.cpp
        plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
    )

    add_library(${PLUGIN_NAME} SHARED ${SRC_FILES})

    if(BUILD_OPENCV)
        add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
    endif()

    target_include_directories(${PLUGIN_NAME} PUBLIC ${OpenCV_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

    target_link_libraries(${PLUGIN_NAME} ${OpenCV_LIBRARIES} Eigen3::Eigen Threads::Threads spdlog::spdlog)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
