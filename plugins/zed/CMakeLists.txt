# To add compiler flags for debugging, uncomment the lines below.
# SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

CMAKE_MINIMUM_REQUIRED(VERSION 3.16)

set(PLUGIN_NAME plugin.zed${ILLIXR_BUILD_SUFFIX})

SET(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(Threads REQUIRED)
find_package(ZED 3 REQUIRED)
find_package(CUDA ${ZED_CUDA_VERSION} EXACT REQUIRED)


set(SRC_FILES plugin.cpp)
set(HDR_FILES include/zed_opencv.hpp
		../../include/illixr/opencv_data_types.hpp
		)

SET(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY})
add_library(${PLUGIN_NAME} SHARED ${HDR_FILES} ${SRC_FILES})

if(BUILD_OPENCV)
	add_dependencies(${PLUGIN_NAME} OpenCV_Viz)
endif()

target_include_directories(${PLUGIN_NAME} PUBLIC ${CUDA_INCLUDE_DIRS} ${ZED_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${Eigen3_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} ${ZED_LIBS} ${OpenCV_LIBRARIES} ${Eigen3_LIBRARIES} Threads::Threads spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
