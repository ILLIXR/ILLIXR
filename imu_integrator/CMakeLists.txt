project(ImuIntegrator)
cmake_minimum_required(VERSION 3.17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(plugin SHARED plugin.cpp)

find_package(GTSAMCMakeTools REQUIRED)
find_package(Eigen3 REQUIRED)

target_include_directories(plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GTSAM_INCLUDE_DIR} ${EIGEN3_INCLUDE_DIR})
target_link_libraries(plugin PRIVATE gtsam)