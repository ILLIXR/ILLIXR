## To add compiler flags for debugging, uncomment the lines below.
# SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=thread")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

CMAKE_MINIMUM_REQUIRED(VERSION 2.4)
SET(CMAKE_CXX_STANDARD 17)

if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 OLD)
	cmake_policy(SET CMP0015 OLD)
endif(COMMAND cmake_policy)

SET(EXECUTABLE_OUTPUT_PATH ".")
SET(THREADS_PREFER_PTHREAD_FLAG TRUE)

find_package(Threads REQUIRED)
find_package(ZED 3 REQUIRED)
find_package(OpenCV REQUIRED)
find_package(CUDA ${ZED_CUDA_VERSION} EXACT REQUIRED)

include_directories(${CUDA_INCLUDE_DIRS})
include_directories(${ZED_INCLUDE_DIRS})
include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

link_directories(${ZED_LIBRARY_DIR})
link_directories(${OpenCV_LIBRARY_DIRS})
link_directories(${CUDA_LIBRARY_DIRS})

FILE(GLOB_RECURSE SRC_FILES src/main.cpp)
FILE(GLOB_RECURSE HDR_FILES include/*.hpp)

SET(ZED_LIBS ${ZED_LIBRARIES} ${CUDA_CUDA_LIBRARY} ${CUDA_CUDART_LIBRARY})

add_library(plugin SHARED ${HDR_FILES} ${SRC_FILES})
target_link_libraries(plugin ${ZED_LIBS} ${OpenCV_LIBRARIES} Threads::Threads)
