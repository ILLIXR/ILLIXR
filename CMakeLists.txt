########################################################################################
#
# This is the install script for ILLIXR
# This will compile the main ILLIXR binary along with any requested plugins.
# The currently supported plugins are:
#   audio_pipeline, debugview, gldemo, ground_truth_slam, gtsam_integrator, kimera_vio,
#   monado, offline_cam, offline_imu, offload_data, openxr_app, orb_slam, pose_prediction,
#   realsense, timewarp_gl
#
# In addition to the standard CMake options, the following arguments can be used:
#
# BUILD_GROUP [ALL|NATIVE|MONADO|CI|TESTS|HEADLESS|NONE] : which subgroup of plugins are to be built
#     ALL : all plugins, this is the default
#     NATIVE : audio_pipeline, debugview, gldemo, gtsam_integrator, ground_truth_slam,
#              kimera_vio, offline_imu, offline_cam, offload_data, pose_prediction, timewarp_gl
#     MONADO : audio_pipeline, gtsam_integrator, kimera_vio, monado, offline_imu, offline_cam,
#              openxr_app, pose_prediction, timewarp_gl
#     CI : audio_pipeline, gldemo, ground_truth_slam, gtsam_integrator, kimera_vio,
#          offline_imu, offline_cam, pose_prediction,  timewarp_gl
#     TESTS:
#     HEADLESS: offline_imu,offline_cam,kimera_vio,gtsam_integrator,pose_prediction,ground_truth_slam,gldemo,timewarp_gl,audio_pipeline
#     NONE : no group is built, individual plugins can be selected
#
# To build specific plugins use the following format:
#   USE_<PLUGIN>=ON
# where <PLUGIN> is the name of the plugin in upper case.
#
########################################################################################

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
set(PACKAGE_STRING "ILLIXR 3.2.0")
set(PACKAGE_VERSION_MAJOR "3")
set(PACKAGE_VERSION_MINOR "2")
set(PACKAGE_VERSION_PATCH "0")
set(PACKAGE_VERSION_LABEL "")

# determine the Linux vendor, if it is CentOS then we must rebuild OpenCV to include all the needed components
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
include(ExternalProject)

project(ILLIXR VERSION 3.2.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DGLSL_VERSION="330")

set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -Wall -Wextra -fPIC")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -fPIC")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-ggdb -O3 -Wall -Wextra -fPIC")

# set the CMake search path to include the local files
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH}")

set(BUILD_GROUP "NONE" CACHE STRING "Which components to build: ALL, NATIVE, MONADO, CI, TESTS, HEADLESS, NONE")
option(DATA_FILE "The data file to get" ON)
set(DATA_FILE "http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip")

# full list of available local plugins
set(PLUGIN_LIST debugview depthai gldemo ground_truth_slam gtsam_integrator offline_cam offline_imu offload_data offload_vio passthrough_integrator pose_lookup pose_prediction realsense record_imu_cam rk4_integrator timewarp_gl)
# full list of available external plugins
set(EXT_PLUGIN_LIST Kimera_vio Audio_Pipeline OpenXR_APP ORB_SLAM Monado)
set(YAML_FILE "" CACHE STRING "Yaml file to specify build parameters")

# plugins, only exposed if BUILD_GROUP is OFF
foreach(ITEM IN LISTS PLUGIN_LIST EXT_PLUGIN_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    option("${ITEM_UPPER}" "" OFF)
endforeach()

set(CORE_PLUGINS OFF)
set(RT_SLAM_PLUGINS OFF)

# read the provided yaml file for arguments/options
if(YAML_FILE)
    set(ENABLE_OFFLOAD "false")
    set(ENABLE_ALIGNMENT "false")
    set(ENABLE_VERBOSE_ERRORS "false")
    set(ENABLE_PRE_SLEEP "false")
    file(STRINGS ${YAML_FILE} YAML_LINES)
    foreach(SITEM IN LISTS YAML_LINES)
        string(REPLACE " " "" ITEM ${SITEM})
        string(REPLACE ":" ";" ITEM_LIST ${ITEM})
        list(GET ITEM_LIST 0 YCOMMAND)
        if(YCOMMAND STREQUAL "group" AND BUILD_GROUP STREQUAL "NONE")
            list(GET ITEM_LIST 1 GROUP_NAME)
            string(TOUPPER ${GROUP_NAME} BUILD_GROUP)
        elseif(YCOMMAND STREQUAL "plugins")
            list(GET ITEM_LIST 1 PLUGIN_NAMES)
            string(REPLACE "," ";" YPLUGIN_LIST ${PLUGIN_NAMES})
            foreach(PL IN LISTS YPLUGIN_LIST)
                string(TOUPPER "USE_${PL}" PL_UPPER)
                set(${PL_UPPER} ON)
            endforeach()
        elseif(YCOMMAND STREQUAL "data")
            list(GET ITEM_LIST 1 DATA_FILE)
        elseif (YCOMMAND STREQUAL "build_type")
            if(CMAKE_BUILD_TYPE STREQUAL "")
                list(GET ITEM_LIST 1 BTYPE)
                set(CMAKE_BUILD_TYPE ${BTYPE})
            endif()
        elseif(YCOMMAND STREQUAL "install_prefix")
            if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
                list(GET ITEM_LIST 1 IPREFIX)
                set(CMAKE_INSTALL_PREFIX ${IPREFIX})
            endif()
        elseif(YCOMMAND STREQUAL "enable_offload")
            list(GET ITEM_LIST 1 IPREFIX)
            set(ENABLE_OFFLOAD ${IPREFIX})
        elseif(YCOMMAND STREQUAL "enable_alignment")
            list(GET ITEM_LIST 1 EALI)
            set(ENABLE_ALIGNMENT ${EALI})
        elseif(YCOMMAND STREQUAL "enable_verbose_errors")
            list(GET ITEM_LIST 1 EVERBE)
            set(ENABLE_VERBOSE_ERRORS ${EVERBE})
        elseif(YCOMMAND STREQUAL "enable_pre_sleep")
            list(GET ITEM_LIST 1 EPRES)
            set(ENABLE_PRE_SLEEP ${EPRES})
        elseif(YCOMMAND STREQUAL "demo_data")
            list(GET ITEM_LIST 1 DDATA)
            set(DEMO_DATA ${DDATA})
        endif()

    endforeach()
endif()

set(ILLIXR_BUILD_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "")
    message(NOTICE "No build type specified, defaulting to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()

# the plugin library names will depend on the build type
string(TOLOWER "${CMAKE_BUILD_TYPE}" lower_type)
if(lower_type MATCHES "debug")
    set(ILLIXR_BUILD_SUFFIX ".dbg")
elseif(lower_type MATCHES "release")
    set(ILLIXR_BUILD_SUFFIX ".opt")
elseif(lower_type MATCHES "relwithdebinfo")
    set(ILLIXR_BUILD_SUFFIX ".optdbg")
endif()

set(CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/lib64/cmake")
set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/lib64/pkgconfig")

link_directories(BEFORE ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib64)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/include)

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
    foreach(ITEM IN LISTS PLUGIN_LIST EXT_PLUGIN_LIST)
        string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
        set(${ITEM_UPPER} ON)
    endforeach()
elseif(BUILD_GROUP STREQUAL "HEADLESS")
    set(RT_SLAM_PLUGINS ON)
    set(CORE_PLUGINS ON)
    set(USE_GROUND_TRUTH_SLAM ON)
    set(USE_GLDEMO ON)
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

find_package(PkgConfig REQUIRED)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source build prohibited.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake")

add_definitions(-DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX})
add_executable(main${ILLIXR_BUILD_SUFFIX}.exe src/main.cpp)

##############
# dependencies
##############
set(EXTERNAL_LIBRARIES "")

# check for 3rd party dependencies that are not parts of OS repos
macro(get_external proj)
    list(APPEND EXTERNAL_LIBRARIES "${proj}")
    set(${proj}_EXTERNAL No)
    set(${proj}_DEP_STR "")
    include(${CMAKE_SOURCE_DIR}/cmake/Get${proj}.cmake)
endmacro()

# check for 3rd party dependencies that are not parts of OS repos, but from included plugins
macro(get_external_for_plugin proj)
    list(APPEND EXTERNAL_LIBRARIES "${proj}")
    set(${proj}_EXTERNAL No PARENT_SCOPE)
    set(${proj}_DEP_STR "" PARENT_SCOPE)
    include(${CMAKE_SOURCE_DIR}/cmake/Get${proj}.cmake)
    set(EXTERNAL_LIBRARIES ${EXTERNAL_LIBRARIES} PARENT_SCOPE)
    set(${proj}_EXTERNAL ${${proj}_EXTERNAL} PARENT_SCOPE)
    set(${proj}_DEP_STR ${${proj}_DEP_STR} PARENT_SCOPE)
endmacro()

##############
# yaml parser
##############

get_external(Yamlcpp)

##############
# opencv
#
# If building on centos system, opencv must be built from scratch to get the viz module
# which is not supplied by the centos repos
##############
set(BUILDING_OPENCV No)
set(OpenCV_DEP_STR "")
if(HAVE_CENTOS)
    set(INTERNAL_OPENCV ${CMAKE_INSTALL_PREFIX}/lib/cmake/opencv4)
    # see if it has been built yet
    find_package(OpenCV QUIET PATHS ${INTERNAL_OPENCV} NO_DEFAULT_PATH NO_CMAKE_ENVIRONMENT_PATH NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_PACKAGE_REGISTRY NO_CMAKE_SYSTEM_PATH NO_CMAKE_SYSTEM_PACKAGE_REGISTRY)
    if(NOT OpenCV_FOUND OR NOT OPENCV_VIZ_FOUND)
	    message("Building OpenCV")
        include(cmake/GetOpenCV_Viz.cmake)
        set(USE_V4_CODE Yes)
        add_dependencies(main${ILLIXR_BUILD_SUFFIX}.exe OpenCV_Viz)
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

find_package(GLEW REQUIRED)

pkg_check_modules(glu REQUIRED glu)

find_package(SQLite3 REQUIRED)

find_package(X11 REQUIRED)

pkg_check_modules(gl REQUIRED gl)

find_package(Eigen3 REQUIRED)

#############
# plugin specific dependencies
#############

find_package(Git REQUIRED)

if (BUILD_GROUP STREQUAL "TESTS")
    find_package(GTest REQUIRED)
endif()

find_package(Boost REQUIRED COMPONENTS filesystem serialization)


if(USE_GTSAM_INTEGRATOR OR USE_KIMERA_VIO)
    get_external(GTSAM)
endif()

target_include_directories(main${ILLIXR_BUILD_SUFFIX}.exe PUBLIC ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${SQLite3_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${yaml-cpp_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(main${ILLIXR_BUILD_SUFFIX}.exe ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${SQLite3_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES} ${OpenCV_LIBRARIES} dl pthread stdc++fs ${yaml-cpp_LIBRARIES})

# set any plugins that are being built
foreach(ITEM IN LISTS PLUGIN_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    if(${ITEM_UPPER})
        add_subdirectory(plugins/${ITEM})
    endif()
endforeach()

# external plugin dependecnies
if(USE_KIMERA_VIO OR USE_ORB_SLAM)
    get_external(DBoW2)
endif()

# external plugins
foreach(ITEM IN LISTS EXT_PLUGIN_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    if(${ITEM_UPPER})
        include(cmake/Get${ITEM}.cmake)
    endif()
endforeach()

if(DATA_FILE)
    if(EXISTS "./data.zip")
        message("Data file already exists, not re-downloding")
    else()
        message("Downloading ${DATA_FILE} to data.zip")
        file(DOWNLOAD ${DATA_FILE} ./data.zip SHOW_PROGRESS)
        if(${CMAKE_MINOR_VERSION} LESS 18)
            execute_process(COMMAND unzip -d ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_CURRENT_SOURCE_DIR}/data.zip)
        else()
            file(ARCHIVE_EXTRACT INPUT ./data.zip DESTINATION ${CMAKE_SOURCE_DIR}/data)
        endif()
    endif()
endif()

install(TARGETS main${ILLIXR_BUILD_SUFFIX}.exe DESTINATION bin)

include(cmake/ConfigurationSummary.cmake)
#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
