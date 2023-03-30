cmake_minimum_required(VERSION 3.16)
set(CMAKE_VERBOSE_MAKEFILE True)
if (POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif (POLICY CMP0048)

if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
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

set(CMAKE_BUILD_TYPE RelWithDebInfo)

project(ILLIXR VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -Wall -Wextra -DGLSL_VERSION='\"330 core\"' -fPIC")
set(CMAKE_CXX_FLAGE_RELEASE "-O3 -DNDEBUG -Wall -Wextra -DGLSL_VERSION='\"330 core\"' -fPIC")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-ggdb -O3 -Wall -Wextra -DGLSL_VERSION='\"330 core\"' -fPIC")
set(CMAKE_INSTALL_PREFIX "/home/friedel/ilxr")
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake")

option(BUILD_NATIVE "" OFF)
option(BUILD_WITH_MONADO "" OFF)
option(BUILD_CI "" OFF)


set(RT_SLAM_PLUGINS OFF)
set(USE_OFFLINE_CAM OFF)
set(USE_OFFLINE_IMU OFF)
set(USE_KIMERA_VIO OFF)
set(USE_GTSAM_INTEGRATOR OFF)
set(USE_POSE_PREDICTION OFF)

set(USE_GROUND_TRUTH_SLAM OFF)
set(USE_GLDEMO OFF)
set(USE_DEBUGVIEW OFF)
set(USE_OFFLOAD_DATA OFF)

set(CORE_PLUGINS OFF)
set(USE_TIMEWARP OFF)
set(USE_AUDIO_PIPELINE OFF)

set(USE_MONADO OFF)
set(USE_OPENXR_APP OFF)

set(USE_DEPTHAI OFF)
#STILL NEED DEMO DATA
if(BUILD_NATIVE)
    set(RT_SLAM_PLUGINS ON)
    set(USE_GROUND_TRUTH_SLAM ON)
    set(USE_GLDEMO ON)
    set(USE_DEBUGVIEW ON)
    set(USE_OFFLOAD_DATA ON)
    set(CORE_PLUGINS ON)
elseif(BUILD_CI)
    set(RT_SLAM_PLUGINS ON)
    set(USE_GROUND_TRUTH_SLAM ON)
    set(USE_GLDEMO ON)
    set(CORE_PLUGINS ON)
    set(USE_AUDIO_PLUGIN ON)
elseif(BUILD_WITH_MONADO)
    set(RT_SLAM_PLUGINS ON)
    set(CORE_PLUGINS ON)
    set(USE_MONADO ON)
    set(USE_OPENXR_APP ON)
elseif(BUILD_TESTS)
    set(USE_AUDIO_PIPELINE ON)
    set(USE_KIMERA_VIO ON)
endif()


if(RT_SLAM_PLUGINS)
    set(USE_OFFLINE_CAM ON)
    set(USE_OFFLINE_IMU ON)
    set(USE_KIMERA_VIO ON)
    set(USE_GTSAM_INTEGRATOR ON)
    set(USE_POSE_PREDICTION ON)
endif()

if(CORE_PLUGINS)
    set(USE_TIMEWARP ON)
    set(USE_AUDIO_PIPELINE ON)
endif()

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
include(ExternalProject)
###### SET AS DEPENDENT
option(BUILD_DBOW2 "Download and build DBow2 as part of this build" OFF)
option(BUILD_DEPTHAI "Download and build depthai as part of this build" OFF)
option(BUILD_KIMERA "Download and build Kimera as part of this build" OFF)
option(USE_REALSENSE "Make sure librealsense is available" OFF)
option(BUILD_TESTS "Build the tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY runtime)

# dependencies

# opencv
find_package(OpenCV REQUIRED CONFIG)

#git
find_package(Git REQUIRED)

#glew
find_package(GLEW REQUIRED)

#googletest
if (BUILD_TESTS)
    find_package(GTest REQUIRED)
endif(BUILD_TESTS)

#boost     CHECK USES (offload_data)
find_package(Boost REQUIRED COMPONENTS filesystem)

#eigen3     #gtsam_integrator
find_package(Eigen3 REQUIRED)

#find_path(FREEGLUT_INCLUDE_DIR "GL/freeglut.h")
#if (NOT FREEGLUT_INCLUDE_DIR)
#    message(FATAL_ERROR "Could not locate freeglut.h, please be sure freeglut3-dev is installed")
#endif(NOT FREEGLUT_INCLUDE_DIR)


pkg_check_modules(glu REQUIRED glu)

#pkg_check_modules(hidapi-hidraw REQUIRED hidapi-hidraw)

#pkg_check_modules(hidapi-libusb REQUIRED hidapi-libusb)

if(USE_REALSENSE)
    find_package(realsense2 REQUIRED)
    find_package(realsense2-gl REQUIRED)
endif(USE_REALSENSE)


find_package(SQLite3 REQUIRED)

find_package(X11 REQUIRED)

pkg_check_modules(gl REQUIRED gl)

EXTERNALPROJECT_ADD(GTSAM
        GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git
        GIT_TAG 9a7d05459a88c27c65b93ea75b68fa1bc0fc0e4b
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DGTSAM_WITH_TBB=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON -DGTSAM_POSE3_EXPMAP=ON -DGTSAM_ROT3_EXPMAP=ON -DGTSAM_WITH_EIGEN_UNSUPPORTED=ON -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF
        INSTALL_COMMAND make install && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsam${CMAKE_BUILD_TYPE}.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam.so && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstable${CMAKE_BUILD_TYPE}.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstable.so)
set(GTSAM_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include")

EXTERNALPROJECT_GET_PROPERTY(GTSAM install_dir)
add_library(gtsam SHARED IMPORTED)
set_property(TARGET gtsam PROPERTY IMPORTED_LOCATION ${install_dir}/src/GTSAM-build/gtsam/libgtsam${CMAKE_BUILD_TYPE}.so)
add_dependencies(gtsam GTSAM)

add_executable(main.opt.exe runtime/main.cpp)
target_include_directories(main.opt.exe PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${SQLite3_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(main.opt.exe ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${SQLite3_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${OpenCV_LIBRARIES} dl pthread stdc++fs)

if(USE_OFFLINE_IMU)
    add_subdirectory(plugins/offline_imu)
endif()
if(USE_OFFLINE_CAM)
    add_subdirectory(plugins/offline_cam)
endif()
if(USE_GROUND_TRUTH_SLAM)
    add_subdirectory(plugins/ground_truth_slam)
endif()
if(USE_POSE_PREDICTION)
    add_subdirectory(plugins/pose_prediction)
endif()
if(USE_GLDEMO)
    add_subdirectory(plugins/gldemo)
endif()
add_subdirectory(plugins/offload_data)

if(USE_TIMEWARP)
    add_subdirectory(plugins/timewarp_gl)
endif()

if(USE_GTSAM_INTEGRATOR)
    add_subdirectory(plugins/gtsam_integrator)
endif()
if(USE_DEPTHAI)
    add_subdirectory(plugins/depthai)
endif()

if(USE_KIMERA_VIO)
    EXTERNALPROJECT_ADD(opengv
            GIT_REPOSITORY https://github.com/laurentkneip/opengv.git
            GIT_TAG 91f4b19c73450833a40e463ad3648aae80b3a7f3
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            )

    # DBoW2
    if(BUILD_DBOW2)
        EXTERNALPROJECT_ADD(DBoW2
                GIT_REPOSITORY https://github.com/dorian3d/DBoW2.git
                GIT_TAG 3924753db6145f12618e7de09b7e6b258db93c6e
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                )

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

    #Kimera
    if (BUILD_KIMERA)
        ExternalProject_Add(KimeraRPGO
             URL https://github.com/MIT-SPARK/Kimera-RPGO/archive/refs/tags/dec-2020.tar.gz
             URL_HASH MD5=26ac6e1525523b88d555e61ca74da34b
             DEPENDS GTSAM
             PREFIX ${CMAKE_BINARY_DIR}/kimerarpgo
             CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        )
    #EXTERNALPROJECT_ADD_STEPDEPENDENCIES(KimeraRPGO configure GTSAM)
    #EXTERNALPROJECT_ADD_STEPDEPENDENCIES(KimeraRPGO build GTSAM)

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


    ExternalProject_Add(Kimera_VIO
            GIT_REPOSITORY https://github.com/ILLIXR/Kimera-VIO.git
            GIT_TAG 37cdaac341334deac83433a428b829c777ec69af
            GIT_SUBMODULES_RECURSE TRUE
            DEPENDS KimeraRPGO DBoW2 GTSAM opengv
            PREFIX ${CMAKE_BINARY_DIR}/kimera_vio
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}  -DCMAKE_SHARED_LINKER_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib -DCMAKE_EXE_LINKER_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib -DBUILD_TESTING=OFF -DBUILD_TESTS=OFF
            )

    #EXTERNALPROJECT_ADD_STEPDEPENDENCIES(Kimera_VIO configure KimeraRPGO GTSAM opengv DBoW2)
    #EXTERNALPROJECT_ADD_STEPDEPENDENCIES(Kimera_VIO build KimeraRPGO GTSAM opengv DBoW2)
endif()

if(USE_AUDIO_PIPELINE)
    ExternalProject_Add(Audio_Pipeline
            GIT_REPOSITORY https://github.com/ILLIXR/audio_pipeline.git
            GIT_TAG 960283cea2c724b73f7cd26ee972a3e5d92c82d0
            PREFIX ${CMAKE_BINARY_DIR}/audio_pipeline
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DILLIXR_INTEGRATION=ON -DILLIXR_ROOT=${PROJECT_SOURCE_DIR}/runtime
            )
endif()

if(USE_MONADO)
    #glslang
    pkg_check_modules(glslang REQUIRED glslang)
    find_package(gflags REQUIRED)
    find_package(JPEG REQUIRED)
    find_package(PNG REQUIRED)
    find_package(TIFF REQUIRED)
    pkg_check_modules(udev REQUIRED udev)
    pkg_check_modules(wayland-server REQUIRED wayland-server)
    pkg_check_modules(x11-xcb REQUIRED x11-xcb)
    pkg_check_modules(xcb-glx REQUIRED xcb-glx)
    pkg_check_modules(xcb-randr REQUIRED xcb-randr)
    pkg_check_modules(xkbcommon REQUIRED xkbcommon)
    pkg_check_modules(xrandr REQUIRED xrandr)
    find_package(OpenXR REQUIRED)
    find_package(Vulkan REQUIRED)

    pkg_check_modules(libusb-1.0 REQUIRED libusb-1.0)

    EXTERNALPROJECT_ADD(Monado
            GIT_REPOSITORY https://github.com/ILLIXR/monado_integration.git
            GIT_TAG 5b91cfc4779d04fcb7742fc9137cdf572aa0fc4b
            PREFIX ${CMAKE_BINARY_DIR}/monado
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_PATH=${CMAKE_SOURCE_DIR}/runtime -DBUILD_WITH_LIBUDEV=0 -DBUILD_WITH_LIBUSB=0 -DBUILD_WITH_LIBUVC=0 -DBUILD_WITH_NS=0 -DBUILD_WITH_PSMV=0 -DBUILD_WITH_PSVR=0 -DBUILD_WITH_OPENHMD=0 -DBUILD_WITH_VIVE=0
            )
endif()

if(USE_OPENXR_APP)
    find_package(SDL2 REQUIRED)

    EXTERNALPROJECT_ADD(OpenXR_App
            GIT_REPOSITORY https://gitlab.freedesktop.org/monado/demos/openxr-simple-example.git
            GIT_TAG 94f1a764dd736b23657ff01464ec1518771e8cdc
            PREFIX ${CMAKE_BINARY_DIR}/OpenXR_APP
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            )
endif()

message("${CMAKE_SOURCE_DIR}/.cache")

#file(DOWNLOAD http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip V1_02_medium.zip SHOW_PROGRESS)

install(TARGETS main.opt.exe DESTINATION bin)

#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
