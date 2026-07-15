if(WIN32 OR MSVC)
    message(WARNING "zed is not yet available for windows")
    set(USE_ZED OFF)
else()
    # To add compiler flags for debugging, uncomment the lines below.
    # SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    # SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

    CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

    set(PLUGIN_NAME plugin.zed${ILLIXR_BUILD_SUFFIX})
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.27.0")
        cmake_policy(SET CMP0146 OLD)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28.0")
            cmake_policy(SET CMP0153 OLD)
        endif()
    endif()

    SET(THREADS_PREFER_PTHREAD_FLAG TRUE)

    find_package(Threads REQUIRED)
    find_package(ZED 3 REQUIRED)
    find_package(CUDA ${ZED_CUDA_VERSION} EXACT REQUIRED)

    set(SRC_FILES plugin.cpp
        zed_camera.cpp
        zed_camera_thread.cpp
    )
    set(HDR_FILES include/zed_opencv.hpp
        zed_camera.hpp
        zed_camera_thread.hpp
        plugin.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/opencv_data_types.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/zed_cam.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/pose.hpp
        ${CMAKE_SOURCE_DIR}/include/illixr/data_format/imu.hpp
    )

    SET(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY})
    add_library(${PLUGIN_NAME} SHARED ${HDR_FILES} ${SRC_FILES})

    target_include_directories(${PLUGIN_NAME} PUBLIC ${CUDA_INCLUDE_DIRS} ${ZED_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

    target_link_libraries(${PLUGIN_NAME} ${ZED_LIBS} ${OpenCV_LIBRARIES} Eigen3::Eigen Threads::Threads spdlog::spdlog)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

    install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
endif()
