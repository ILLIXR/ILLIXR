cmake_minimum_required(VERSION 3.16)
set(CMAKE_VERBOSE_MAKEFILE True)
if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)

if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(PACKAGE_NAME "ILLIXR")
set(PACKAGE_STRING "ILLIXR 1.0.0")
set(PACKAGE_VERSION_MAJOR "1")
set(PACKAGE_VERSION_MINOR "0")
set(PACKAGE_VERSION_PATCH "0")
set(PACKAGE_VERSION_LABEL "")

if(PACKAGE_VERSION_LABEL)
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}-${PACKAGE_VERSION_LABEL}")
else()
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}")
endif()

project(ILLIXR VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake;/home/friedel/deps/lib/cmake/DBoW2")


find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)

# Use given `clang` toolchain.
#set(CMAKE_CXX_COMPILER "${LLVM_TOOLS_BINARY_DIR}/clang++")
#set(CMAKE_C_COMPILER "${LLVM_TOOLS_BINARY_DIR}/clang")

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source build prohibited.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake")

include(FetchContent)

option(BUILD_DBOW2 "Download and build DBow2 as part of this build" OFF)
option(BUILD_DEPTHAI "Download and build depthai as part of this build" OFF)
option(BUILD_KIMERA "Download and build Kimera as part of this build" OFF)
option(USE_REALSENSE "Make sure librealsense is available" OFF)
option(BUILD_TESTS "Build the tests" ON)

add_executable(illixr runtime/main.cpp)

# dependencies
# curl
find_package(CURL)

# opencv
find_package(OpenCV REQUIRED CONFIG)

# glut
find_package(GLUT REQUIRED)

#git
find_package(Git REQUIRED)

#glew
find_package(GLEW REQUIRED)

#glslang
pkg_check_modules(glslang REQUIRED glslang)

#googletest
if (BUILD_TESTS)
    find_package(GTest REQUIRED)
endif(BUILD_TESTS)

#libatlas
pkg_check_modules(blas-atlas REQUIRED blas-atlas)
pkg_check_modules(lapack-atlas REQUIRED lapack-atlas)

#libblas
find_package(BLAS REQUIRED)

#boost
find_package(Boost REQUIRED)

#libdrm
pkg_check_modules(libdrm REQUIRED libdrm)

#eigen3
find_package(Eigen3 REQUIRED)

#gtk3
pkg_check_modules(gtk+-3.0 REQUIRED gtk+-3.0)

#gtk2
find_package(GTK2 REQUIRED)



# DBoW2
if(BUILD_DBOW2)
    FetchContent_Declare(DBoW2
            URL https://github.com/dorian3d/DBoW2/archive/refs/tags/v1.1-free.tar.gz
            URL_HASH MD5=2764979e5be6bc7a7f041892d6527af4
            FIND_PACKAGE_ARGS CONFIG)

    FetchContent_MakeAvailable(DBoW2)
else(BUILD_DBOW2)
    find_package(DBoW2 CONFIG)
    if(NOT DBoW2_FOUND)
        message(FATAL_ERROR "Could not find a package configuration file provided by \"DBoW2\" with any of the following names:
    DBoW2Config.cmake
    dbow2-config.cmake
Add the installation prefix of \"DBoW2\" to CMAKE_PREFIX_PATH or set \
\"DBoW2_DIR\" to a directory containing one of the above files or \
 add -DBUILD_DBOW2=ON to the cmake command line to download and \
 build it with this project.")
    endif(NOT DBoW2_FOUND)
endif(BUILD_DBOW2)

include_directories(DBoW2_INCLUDE_DIRS)
target_link_libraries(illixr PUBLIC DBoW2_LIBRARIES)

# depthai
if(BUILD_DEPTHAI)
    FetchContent_Declare(depthai
            URL https://github.com/luxonis/depthai-core/archive/refs/tags/v2.20.2.tar.gz
            URL_HASH MD5=fde3a85c281a6731ff13dc8a18225a87
            FIND_PACKAGE_ARGS CONFIG)

    FetchContent_MakeAvailable(DBoW2)

else(BUILD_DEPTHAI)
    find_package(depthai CONFIG)
    if(NOT depthai_FOUND)
        message(FATAL_ERROR "Could not find a package configuration file provided by \"depthai\" with any of the following names:
    depthaiConfig.cmake
    depthai-config.cmake
Add the installation prefix of \"depthai\" to CMAKE_PREFIX_PATH or set \
\"depthai_DIR\" to a directory containing one of the above files or \
 add -DBUILD_DBOW2=ON to the cmake command line to download and \
 build it with this project.")
    endif(NOT depthai_FOUND)
endif(BUILD_DEPTHAI)

target_link_libraries(illixr PRIVATE depthai::opencv)

find_path(FREEGLUT_INCLUDE_DIR "GL/freeglut.h")
if (NOT FREEGLUT_INCLUDE_DIR)
    message(FATAL_ERROR "Could not locate freeglut.h, please be sure freeglut3-dev is installed")
endif(NOT FREEGLUT_INCLUDE_DIR)


FETCHCONTENT_DECLARE(GTSAM
        GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git
        GIT_TAG ee069286b447ff58b809423cc77c777a02abdfe5
        OVERRIDE_FIND_PACKAGE
        )
FETCHCONTENT_MAKEAVAILABLE(GTSAM)

#Kimera
if (BUILD_KIMERA)
    FetchContent_Declare(KimeraRPGO
            URL https://github.com/MIT-SPARK/Kimera-RPGO/archive/refs/tags/dec-2020.tar.gz
            URL_HASH MD5=26ac6e1525523b88d555e61ca74da34b
            FIND_PACKAGE_ARGS CONFIG)

    FetchContent_MakeAvailable(KimeraRPGO)

else(BUILD_KIMERA)
    find_package(KimeraRPGO)
    if(NOT KimeraRPGO_FOUND)
        message(FATAL_ERROR "Could not find a package configuration file provided by \"KimeraRPGO\" with any of the following names:
    KimeraRPGOConfig.cmake
    KimeraRPGO-config.cmake
Add the installation prefix of \"KimeraRPGO\" to CMAKE_PREFIX_PATH or set \
\"KimeraRPGO_DIR\" to a directory containing one of the above files or \
 add -DBUILD_KIMERA=ON to the cmake command line to download and \
 build it with this project.")
    endif(NOT KimeraRPGO_FOUND)

endif(BUILD_KIMERA)

pkg_check_modules(epoxy REQUIRED epoxy)

pkg_check_modules(gbm REQUIRED gbm)

find_package(gflags REQUIRED)

pkg_check_modules(glu REQUIRED glu)

pkg_check_modules(hidapi-hidraw REQUIRED hidapi-hidraw)

pkg_check_modules(hidapi-libusb REQUIRED hidapi-libusb)

find_package(JPEG REQUIRED)

find_package(Parmetis REQUIRED)

find_package(PNG REQUIRED)

find_package(PulseAudio REQUIRED)

if(USE_REALSENSE)
    find_package(realsense2 REQUIRED)
    find_package(realsense2-gl REQUIRED)
endif(USE_REALSENSE)

find_package(SDL2 REQUIRED)

find_package(SQLite3 REQUIRED)

find_package(OpenSSL REQUIRED)

find_package(SuiteSparse REQUIRED)

find_package(TBB REQUIRED)

find_package(TIFF REQUIRED)

pkg_check_modules(udev REQUIRED udev)

pkg_check_modules(libusb-1.0 REQUIRED libusb-1.0)
pkg_check_modules(libusb REQUIRED libusb)
pkg_check_modules(libv4l1 REQUIRED libv4l1) # which do we need?
pkg_check_modules(virglrenderer REQUIRED virglrenderer)
find_package(VTK REQUIRED CONFIG)
pkg_check_modules(wayland-server REQUIRED wayland-server)
find_package(X11 REQUIRED)
pkg_check_modules(x11-xcb REQUIRED x11-xcb)
pkg_check_modules(xcb-glx REQUIRED xcb-glx)
pkg_check_modules(xcb-randr REQUIRED xcb-randr)
pkg_check_modules(xkbcommon REQUIRED xkbcommon)
pkg_check_modules(xrandr REQUIRED xrandr)
pkg_check_modules(dri REQUIRED dri)
find_package(OpenXR REQUIRED)
#find_package(qemu)
find_package(Vulkan REQUIRED)

#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
