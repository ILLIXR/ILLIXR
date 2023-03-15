project(OpenVINS)
cmake_minimum_required(VERSION 3.17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(plugin SHARED plugin.cpp)

find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED)
find_package(Ceres REQUIRED)

# headers: /usr/local/include/open_vins/
# library: /usr/local/lib/libov_msckf_lib.so
#find_package(ov_msckf_lib REQUIRED PATH /usr/local/lib/)
set(OPENVINS_INCLUDE_DIR /usr/local/include/open_vins/)
set(OPENVINS_LIBRARIES ov_msckf_lib)

# if we should compile openvins to predict a "fast" odometry pose
#add_definitions(-DENABLE_OPENVINS_PREDICT=0)
add_definitions(-DENABLE_OPENVINS_PREDICT=1)

#add_definitions(-Wall -Wextra -Werror)
target_include_directories(plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${EIGEN3_INCLUDE_DIR} ${BOOST_INCLUDE_DIR} ${OPENVINS_INCLUDE_DIR})
target_link_libraries(plugin PRIVATE ${CERES_LIBRARIES} ${OPENVINS_LIBRARIES})
