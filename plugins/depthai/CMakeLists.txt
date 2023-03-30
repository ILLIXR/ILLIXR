cmake_minimum_required(VERSION 3.2)
project(depthai_plugin)
# depthai
if(BUILD_DEPTHAI)
    #FetchContent_Declare(depthai
    #        URL https://github.com/luxonis/depthai-core/archive/refs/tags/v2.20.2.tar.gz
    #        URL_HASH MD5=ce7a671af71d850c4c36edc36d4595e9
    #        FIND_PACKAGE_ARGS CONFIG)

    #FetchContent_MakeAvailable(depthai)
    #    ExternalProject_Add(depthai
    #            URL https://github.com/luxonis/depthai-core/archive/refs/tags/v2.20.2.tar.gz
    #            URL_HASH MD5=ce7a671af71d850c4c36edc36d4595e9
    #            PREFIX ${CMAKE_BINARY_DIR}/depthai
    #            CMAKE_ARGS -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    #            )

else(BUILD_DEPTHAI)
    find_package(depthai CONFIG)
    #    if(NOT depthai_FOUND)
    #        message(FATAL_ERROR "Could not find a package configuration file provided by \"depthai\" with any of the following names:
    #    depthaiConfig.cmake
    #    depthai-config.cmake
    #Add the installation prefix of \"depthai\" to CMAKE_PREFIX_PATH or set \
    #\"depthai_DIR\" to a directory containing one of the above files or \
    # add -DBUILD_DEPTHAI=ON to the cmake command line to download and \
    # build it with this project.")
    #    endif(NOT depthai_FOUND)
endif(BUILD_DEPTHAI)

# Add depthai-core dependency
find_package(depthai CONFIG REQUIRED)

# OpenCV
find_package(OpenCV REQUIRED )
include_directories(${OpenCV_INCLUDE_DIRS} ${DepthAI_INCLUDE_DIRS})

add_definitions(-Wall -Wextra -Werror -Wno-format-extra-args -std=c++17 -g -O3)
add_library(plugin SHARED plugin.cpp)
target_link_libraries(plugin PUBLIC ${DEPENDENCIES} ${OpenCV_LIBRARIES} PRIVATE depthai::core)

install(TARGETS plugin DESTINATION lib)
