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

set(BUILDING_ILLIXR Yes)

set(PACKAGE_NAME "ILLIXR")
set(PACKAGE_STRING "ILLIXR 1.0.0")
set(PACKAGE_VERSION_MAJOR "1")
set(PACKAGE_VERSION_MINOR "0")
set(PACKAGE_VERSION_PATCH "0")
set(PACKAGE_VERSION_LABEL "")

if(UNIX)
    execute_process(COMMAND bash -c "awk -F= '/^ID=/{print $2}' /etc/os-release | tr -d '\n' | tr -d '\"'" OUTPUT_VARIABLE OS_FLAVOR)
    message("Found OS: ${OS_FLAVOR}")
    string(TOLOWER "${OS_FLAVOR}" OS_LOWER)
    if(OS_LOWER STREQUAL "centos")
        set(HAVE_CENTOS Yes)
    else()
        set(HAVE_CENTOS No)
    endif()
endif()

if(PACKAGE_VERSION_LABEL)
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}-${PACKAGE_VERSION_LABEL}")
else()
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}")
endif()

include(CMakeDependentOption)
project(ILLIXR VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DGLSL_VERSION="330")
set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -Wall -Wextra -fPIC")
set(CMAKE_CXX_FLAGE_RELEASE "-O3 -DNDEBUG -Wall -Wextra -fPIC")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-ggdb -O3 -Wall -Wextra -fPIC")
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}")
set(BUILD_GROUP "ALL" CACHE STRING "Which components to build: ALL, NATIVE, MONADO, CI, TESTS, NONE")

set(PLUGIN_LIST debugview depthai gldemo ground_truth_slam gtsam_integrator offline_cam offline_imu offload_data pose_prediction realsense timewarp_gl)
set(EXT_PLUGIN_LIST kimera_vio audio_pipeline openxr_app orb_slam monado)

link_directories(BEFORE ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib64)

# plugins, only exposed if BUILD_GROUP is OFF
foreach(ITEM IN LISTS PLUGIN_LIST EXT_PLUGIN_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    cmake_dependent_option(${ITEM_UPPER} "" OFF "BUILD_GROUP STREQUAL NONE" OFF)
endforeach()

set(CORE_PLUGINS OFF)
set(RT_SLAM_PLUGINS OFF)

#STILL NEED DEMO DATA
if(BUILD_GROUP STREQUAL "NATIVE")
    set(RT_SLAM_PLUGINS ON)
    set(USE_GROUND_TRUTH_SLAM ON)
    set(USE_GLDEMO ON)
    set(USE_DEBUGVIEW ON)
    set(USE_OFFLOAD_DATA ON)
    set(CORE_PLUGINS ON)
    set(USE_ORB_SLAM ON)
elseif(BUILD_GROUP STREQUAL "CI")
    set(RT_SLAM_PLUGINS ON)
    set(USE_GROUND_TRUTH_SLAM ON)
    set(USE_GLDEMO ON)
    set(CORE_PLUGINS ON)
elseif(BUILD_GROUP STREQUAL "MONADO")
    set(RT_SLAM_PLUGINS ON)
    set(CORE_PLUGINS ON)
    set(USE_MONADO ON)
    set(USE_OPENXR_APP ON)
elseif(BUILD_GROUP STREQUAL "TESTS")
    set(USE_AUDIO_PIPELINE ON)
    set(USE_KIMERA_VIO ON)
elseif(BUILD_GROUP STREQUAL "ALL")
    set(RT_SLAM_PLUGINS ON)
    set(CORE_PLUGINS ON)
    set(USE_GROUND_TRUTH_SLAM ON)
    set(USE_GLDEMO ON)
    set(USE_DEBUGVIEW ON)
    set(USE_OFFLOAD_DATA ON)
    set(USE_ORB_SLAM ON)
    set(USE_AUDIO_PLUGIN ON)
    set(USE_MONADO ON)
    set(USE_OPENXR_APP ON)
    set(USE_KIMERA_VIO ON)
else()
    set(SUBTARGETS No)
    foreach(ITEM IN LISTS PLUGIN_LIST EXT_PLUGIN_LIST)
        string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
        if(${ITEM_UPPER})
            set(SUBTARGETS Yes)
        endif()
    endforeach()
    if(NOT SUBTARGETS)
        message(WARNING "BUILD_GROUP is not defined and no Plugins were selected. Only the main binary will be built.")
    endif()
endif()

if(RT_SLAM_PLUGINS)
    set(USE_OFFLINE_CAM ON)
    set(USE_OFFLINE_IMU ON)
    set(USE_KIMERA_VIO ON)
    set(USE_GTSAM_INTEGRATOR ON)
    set(USE_POSE_PREDICTION ON)
endif()

if(CORE_PLUGINS)
    set(USE_TIMEWARP_GL ON)
    set(USE_AUDIO_PIPELINE ON)
endif()

#find_package(LLVM REQUIRED CONFIG)
#find_package(Clang REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source build prohibited.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake")

include(ExternalProject)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY runtime)

# dependencies

# opencv
add_executable(main.opt.exe runtime/main.cpp)
set(BUILDING_OPENCV No)
set(OpenCV_DEP_STR "")
if(HAVE_CENTOS)
    set(INTERNAL_OPENCV ${CMAKE_INSTALL_PREFIX}/lib/cmake/opencv4)
    find_package(OpenCV QUIET PATHS ${INTERNAL_OPENCV} NO_DEFAULT_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_PACKAGE_REGISTRY NO_CMAKE_SYSTEM_PATH NO_CMAKE_SYSTEM_PACKAGE_REGISTRY)
    if(NOT OpenCV_FOUND OR NOT OPENCV_VIZ_FOUND)
	message("Building OpenCV")
        include(cmake/GetOpenCV_Viz.cmake)
        set(USE_V4_CODE Yes)
        add_dependencies(main.opt.exe OpenCV_Viz)
        set(OpenCV_DEP_STR OpenCV_Viz)
        set(INTERNAL_OPENCV ${OpenCV_DIR})
        set(BUILD_OPENCV Yes)
    endif()
else()
    find_package(OpenCV REQUIRED)# CONFIG)
endif()

if(DEFINED OpenCV_VERSION_MAJOR)
    if (${OpenCV_VERSION_MAJOR} EQUAL 4)
        set(USE_V4_CODE Yes)
    else()
        set(USE_V4_CODE No)
    endif()
endif()
#glew
find_package(GLEW REQUIRED)

pkg_check_modules(glu REQUIRED glu)

find_package(SQLite3 REQUIRED)

find_package(X11 REQUIRED)

pkg_check_modules(gl REQUIRED gl)

#plugin dependencies

#git
if(USE_GTSAM_INTEGRATOR OR USE_ORB_SLAM OR USE_KIMERA_VIO OR USE_AUDIO_PIPELINE OR USE_MONADO OR USE_OPENXR_APP)
    find_package(Git REQUIRED)
endif()

#googletest
if (BUILD_GROUP STREQUAL "TESTS")
    find_package(GTest REQUIRED)
endif()

#boost     CHECK USES (offload_data)
if(USE_OFFLOAD_DATA)
    find_package(Boost REQUIRED COMPONENTS filesystem)
elseif(USE_ORB_SLAM)
    find_package(Boost REQUIRED COMPONENTS filesystem serialization)
endif()

#eigen3     #gtsam_integrator
if(USE_GTSAM_INTEGRATOR OR USE_ORB_SLAM)
    find_package(Eigen3 REQUIRED)
endif()

#find_path(FREEGLUT_INCLUDE_DIR "GL/freeglut.h")
#if (NOT FREEGLUT_INCLUDE_DIR)
#    message(FATAL_ERROR "Could not locate freeglut.h, please be sure freeglut3-dev is installed")
#endif(NOT FREEGLUT_INCLUDE_DIR)

set(EXTERNAL_LIBRARIES "")

macro(get_external proj)
    list(APPEND EXTERNAL_LIBRARIES "${proj}")
    set(${proj}_EXTERNAL No)
    set(${proj}_DEP_STR "")
    include(cmake/Get${proj}.cmake)
endmacro()
#pkg_check_modules(hidapi-hidraw REQUIRED hidapi-hidraw)

#pkg_check_modules(hidapi-libusb REQUIRED hidapi-libusb)

if(USE_REALSENSE)
    find_package(realsense2 REQUIRED)
    find_package(realsense2-gl REQUIRED)
endif(USE_REALSENSE)

if(USE_GTSAM_INTEGRATOR OR USE_KIMERA_VIO)
    get_external(GTSAM)
endif()

target_include_directories(main.opt.exe PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${SQLite3_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(main.opt.exe ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${SQLite3_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${OpenCV_LIBRARIES} dl pthread stdc++fs)

foreach(ITEM IN LISTS PLUGIN_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    if(${ITEM_UPPER})
        add_subdirectory(plugins/${ITEM})
    endif()
endforeach()

if(USE_KIMERA_VIO OR USE_ORB_SLAM)
    get_external(DBoW2)
endif()

if(USE_ORB_SLAM)
    #include(cmake/GetPangolin.cmake)
    get_external(Pangolin)
    get_external(g2o)
    get_external(Sophus)
    include(cmake/GetORB_SLAM.cmake)
endif()

if(USE_KIMERA_VIO)
    get_external(opengv)
    get_external(KimeraRPGO)
    include(cmake/GetKimera_vio.cmake)
endif()

if(USE_AUDIO_PIPELINE)
    get_external(PortAudio)
    get_external(SpatialAudio)
    include(cmake/GetAudioPipeline.cmake)
endif()

if(USE_MONADO)
    include(cmake/GetMonado.cmake)
endif()

if(USE_OPENXR_APP)
    include(cmake/GetOpenXR_APP.cmake)
endif()

#file(DOWNLOAD http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip V1_02_medium.zip SHOW_PROGRESS)

install(TARGETS main.opt.exe DESTINATION bin)

include(cmake/ConfigurationSummary.cmake)
#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
