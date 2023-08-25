###############################################
# Top-level CMakeLists.txt for dataset_plugin #
###############################################

cmake_minimum_required(VERSION 3.0)

project(dataset_plugin)
set(CMAKE_BUILD_TYPE RelWithDebInfo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

find_package(Eigen3 REQUIRED)
include_directories(${EIGEN3_INCLUDE_DIR})
message(STATUS "Eigen3 version: ${Eigen3_VERSION}")
message(STATUS "EIGEN Location: ${EIGEN3_INCLUDE_DIR}")
find_package(OpenCV 3.4.6 EXACT REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS})

# Prefer GLVND
if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

if (POLICY CMP0072)
  cmake_policy (SET CMP0054 NEW)
endif(POLICY CMP0072)

set(CMAKE_C_COMPILER "/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

#################################
# Add additional compiler flags #
#################################

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++ -Wno-deprecated-declarations -Wno-unused-function")
endif()

if(NOT MSVC_IDE)
  set(CFLAGS_WARN "-Wall -Wextra -Wno-unused-parameter -Wno-strict-aliasing")
  set(CMAKE_CXX_FLAGS "-fPIC -O3 -std=c++17 -march=native ${CFLAGS_WARN} ${CMAKE_CXX_FLAGS}")
  #set(CMAKE_CXX_FLAGS "-fPIC -g ${CFLAGS_WARN} ${CMAKE_CXX_FLAGS}")
endif()

#########################
# Suppress auto-linking #
#########################

if(MSVC_IDE)
  add_definitions(-DUSING_CMAKE=1)
endif()

######################
# Add subdirectories #
######################

add_library(plugin SHARED src/dataset_loader.cpp src/config.cpp)

if(DEFINED $ENV{ILLIXR_DATASET_USE_IMU_PUBLISHER})
  add_library(plugin SHARED src/imu_publisher_plugin.cpp)
endif()

if(DEFINED $ENV{ILLIXR_DATASET_USE_IMAGE_PUBLISHER})
  add_library(plugin SHARED src/image_publisher_plugin.cpp)
endif()

if(DEFINED $ENV{ILLIXR_DATASET_USE_POSE_PUBLISHER})
  add_library(plugin SHARED src/pose_publisher_plugin.cpp)
endif()

if(DEFINED $ENV{ILLIXR_DATASET_USE_GROUND_TRUTH_PUBLISHER})
  add_library(plugin SHARED src/ground_truth_publisher_plugin.cpp)
endif()

target_include_directories(plugin PUBLIC .)

target_link_libraries(plugin ${OpenCV_LIBS} Eigen3::Eigen)
