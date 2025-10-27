########################################################################################
#
# This is the install script for ILLIXR
# This will compile the main ILLIXR binary along with any requested plugins.
#
# See the documentation at https://illixr.github.io/ILLIXR/getting_started/
# for complete build instructions.#
#
########################################################################################

cmake_minimum_required(VERSION 3.22)

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
set(PACKAGE_STRING "ILLIXR 4.0.0")
set(PACKAGE_VERSION_MAJOR "4")
set(PACKAGE_VERSION_MINOR "0")
set(PACKAGE_VERSION_PATCH "0") 
set(PACKAGE_VERSION_LABEL "")

set(DEPENDENCY_BUILD_SUFFIX "")
if(WIN32 OR MSVC)
    add_compile_definitions("-DWIN_OS")
    set(VCPKG_TARGET_TRIPLET "x64-windows")
    set(VCPKG_POLICY_ALLOW_EXES_IN_BIN enabled)
    set(DEPENDENCY_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    set(DEPENDENCY_BUILD_SUFFIX "$<IF:$<CONFIG:Debug>,d, >")
elseif(APPLE)
    message(FATAL_ERROR "MacOS is currently not supported")
elseif(NOT UNIX)
    message(FATAL_ERROR "Unknown operating system")
else()
    set(DEPENDENCY_BUILD_TYPE Release)
    #set(CMAKE_RC_COMPILER "D:/llvm/bin/llvm-rc.exe" CACHE FILEPATH "" FORCE)
    #set(PKG_CONFIG_EXECUTABLE "D:/pkg-config/bin/pkg-config.exe" CACHE FILEPATH "" FORCE)
endif()

set(GPL_LICENSED_CODE "openvins;orb_slam3;offload_rendering_server;offload_rendering_client")
set(LGPL_LICENSED_CODE "audio_pipeline")

#set(yaml-cpp_DIR "${CMAKE_SOURCE_DIR}/out/install/x64-Debug/lib/cmake/yaml-cpp") 

if(PACKAGE_VERSION_LABEL)
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}-${PACKAGE_VERSION_LABEL}")
else()
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}")
endif()

include(FetchContent)
include(CMakeDependentOption)

project(ILLIXR VERSION 3.2.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DGLSL_VERSION="330")

# Vulkan flags
add_compile_definitions(VK_ENABLE_BETA_EXTENSIONS)

if(WIN32 OR MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /MDd")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "/DEBUG /STACK:8388608")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /O2")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-ggdb -O3 -Wall")
endif()

# check for the Jetson platform
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" AND EXISTS "/etc/nv_tegra_release")
    set(ON_JETSON_PLATFORM YES)
else()
    set(ON_JETSON_PLATFORM NO)
endif()

# needed to generate runtime scripts
set(OPENXR_RUNTIME "")

# set the CMake search path to include the local files
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH})
option(COLOR_OUTPUT "Produce color output to the screen" ON)
option(DATA_FILE "The data file to get" ON)
option(BUILD_OXR_INTERFACE "Build the OpenXR interface (hand tracking only)" OFF)
cmake_dependent_option(BUILD_OXR_TEST "Build the  test for the OpenXr interface (hand tracking only)" OFF BUILD_OXR_INTERFACE OFF)
option(BUILD_ZED_CAPTURE "Build the ZED capture program" OFF)
set(DATA_FILE "http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip")

# docs variables
option(BUILD_DOCS "Build documentation" OFF)
option(BUILD_DOCS_FOR_RELEASE "Build the documentation in preparation for an official release" OFF)
option(MKDOCS_LOCATION "The path of the mkdocs executable" "")
option(PIP_LOCATION "The path of the pip script" "")
mark_as_advanced(FORCE MKDOCS_LOCATION PIP_LOCATION BUILD_DOCS_FOR_RELEASE)

option(USE_MONADO "Build for monado use" OFF)
option(NO_GPL "Do not build any code licensed under GPL. This will affect any generated profile files as well." OFF)
option(NO_LGPL "Do not build any code licensed under LGPL or GPL. This will affect any generated profile files as well." OFF)
option(NO_ZED "Do not build and ZED related code" OFF)

set(LAST_NO_GPL "$CACHE{NO_GPL_CACHE}")
set(LAST_NO_LGPL "$CACHE{NO_LGPL_CACHE}")

if(NO_LGPL)
    set(NO_GPL ON CACHE BOOL "" FORCE)
endif()
string(LENGTH "${LAST_NO_GPL}" GPL_LEN)
string(LENGTH "${LAST_NO_LGPL}" LGPL_LEN)
if((GPL_LEN EQUAL 0) OR (LGPL_LEN EQUAL 0) OR (NOT NO_GPL STREQUAL LAST_NO_GPL) OR (NOT NO_LGPL STREQUAL LAST_NO_LGPL))
    set(FORCE_REGEN ON)
endif()

set(NO_GPL_CACHE ${NO_GPL} CACHE INTERNAL "")
set(NO_LGPL_CACHE ${NO_LGPL} CACHE INTERNAL "")
option(HT_ENABLE_GPU "Build for GPU use" OFF)
if(HT_ENABLE_GPU)
    message(WARNING "Currently gpu processing for hand tracking is not available, turning it off")
    set(HT_ENABLE_GPU OFF)
endif()
option(BUILD_PARALLEL_LEVEL "Number of parallel build jobs")
option(ENABLE_MONADO "Build monado package" OFF)
if(NOT BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)
    if(N GREATER 8)
        set(BUILD_PARALLEL_LEVEL 4)
    elseif(N GREATER 6)
        set(BUILD_PARALLEL_LEVEL 2)
    else()
        set(BUILD_PARALLEL_LEVEL 1)
    endif()
endif()

include(cmake/HelperFunctions.cmake)

generate_yaml()
check_plugins()

set(CORE_PLUGINS OFF)
set(RT_SLAM_PLUGINS OFF)
if(YAML_FILE)
    read_yaml(${YAML_FILE})
    string(REGEX MATCH "/all.yaml" MATCH_FOUND ${YAML_FILE})
    if (${YAML_FILE} STREQUAL "all.yaml" OR MATCH_FOUND)
        set(BUILD_DEP_MAP ON)
    endif()
    if(LOCAL_BUILD_FLAGS)
        add_compile_definitions("${LOCAL_BUILD_FLAGS}")
        string(REPLACE " " ";" TEMP_BUILD_FLAGS "${LOCAL_BUILD_FLAGS}")
        foreach(B_FLAG IN LISTS TEMP_BUILD_FLAGS)
            set(${B_FLAG} ON)
        endforeach()
    endif()
endif()

set(ILLIXR_BUILD_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "")
    message(NOTICE "No build type specified, defaulting to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()

# the plugin library names will depend on the build type
string(TOLOWER "${CMAKE_BUILD_TYPE}" lower_type)
if(lower_type MATCHES "debug")
    set(DO_DEBUG Yes)
    set(ILLIXR_BUILD_SUFFIX ".dbg")
elseif(lower_type MATCHES "release")
    set(ILLIXR_BUILD_SUFFIX ".opt")
elseif(lower_type MATCHES "relwithdebinfo")
    set(DO_DEBUG Yes)
    set(ILLIXR_BUILD_SUFFIX ".optdbg")
endif()

if(WIN32 OR MSVC)
    set(PKG_CONFIG_EXECUTABLE "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf.exe")
    find_package(PkgConfig REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
endif()

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source build prohibited.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake;${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/share/cmake)

add_definitions(-DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX})
##############
# dependencies
##############
set(EXTERNAL_LIBRARIES "")
find_package(OpenCV 4 REQUIRED)


#############
# plugin specific dependencies that are used by several plugins
#############

if(WIN32 OR MSVC)
    find_package(OpenGL REQUIRED)
    pkg_check_modules(X11 REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(glslang REQUIRED)
else()
    find_package(X11 REQUIRED)
    pkg_check_modules(gl REQUIRED gl)
    pkg_check_modules(glu REQUIRED glu)
    get_external(Vulkan)
endif()

find_package(SQLite3 REQUIRED)
find_package(Eigen3 REQUIRED CONFIG)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED CONFIG)
find_package(protobuf REQUIRED CONFIG)

find_package(Git REQUIRED)
if(WIN32 OR MSVC)
    set(Boost_USE_STATIC_LIBS ON)
    add_definitions(-DBOOST_ALL_NO_LIB)
    add_definitions(-DBOOST_SERIALIZATION_DYN_LINK)
endif()
if (CMAKE_VERSION VERSION_LESS "3.30.0")
    find_package(Boost REQUIRED COMPONENTS filesystem serialization iostreams)
else()
    find_package(Boost REQUIRED COMPONENTS filesystem serialization iostreams CONFIG)
endif()

find_package(spdlog REQUIRED CONFIG)

add_definitions(-DFMT_HEADER_ONLY)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads)

# get any env flags for the build
if($ENV{ILLIXR_BUILD_FLAGS})
    string(REPLACE "-D" "" CLI_BUILD_FLAGS "$ENV{ILLIXR_BUILD_FLAGS}")
    string(REPLACE " " "" CLI_BUILD_FLAGS "${CLI_BUILD_FLAGS}")
    foreach(FLAG IN LISTS CLI_BUILD_FLAGS)
        string(REPLACE "=" ";" FLAG_LIST "${FLAG}")
        list(GET FLAG_LIST 0 ITEM)
        list(GET FLAG_LIST 1 VALUE)
        set(${ITEM} ${VALUE} CACHE BOOL "" FORCE)
    endforeach()
endif()

if(NO_GPL)
    if(NO_LGPL)
        foreach(ITEM IN LISTS LGPL_LICENSED_CODE)
            string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
            set(${ITEM_UPPER} OFF)
        endforeach()
    endif()
    foreach(ITEM IN LISTS GPL_LICENSED_CODE)
        string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
        set(${ITEM_UPPER} OFF)
    endforeach()
endif()
if(NO_ZED)
    set(BUILD_ZED_CAPTURE OFF)
    set(USE_ZED OFF)
    set(USE_ZED_DATA_INJECTION OFF)
endif()
# set any plugins that are being built
set(FULL_PLUGIN_LIST "")

add_subdirectory(utils)
if(BUILD_DOCS)
    set(DOCS_LIST "${CMAKE_SOURCE_DIR}/include/illixr;${CMAKE_SOURCE_DIR}/include/illixr/data_format;${CMAKE_SOURCE_DIR}/include/illixr/network;${CMAKE_SOURCE_DIR}/include/illixr/vk;${CMAKE_SOURCE_DIR}/plugins/hand_tracking/viewer;${CMAKE_SOURCE_DIR}/plugins/zed/data_injection;${CMAKE_SOURCE_DIR}/plugins/zed/capture;${CMAKE_SOURCE_DIR}/src;${CMAKE_SOURCE_DIR}/src/display;${CMAKE_SOURCE_DIR}/utils")
    set(DOCS_EXCLUDE_LIST "${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp")
endif()
foreach(ITEM IN LISTS PLUGIN_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    string(TOLOWER ${ITEM} ITEM_LOWER)
    list(APPEND FULL_PLUGIN_LIST ${ITEM})
    string(REPLACE "." "/" ITEM_LOWER_SUB "${ITEM_LOWER}")
    if(EXISTS "${CMAKE_SOURCE_DIR}/plugins/${ITEM_LOWER_SUB}")
        set(CURRENT_PLUGIN_SRC "plugins/${ITEM_LOWER_SUB}")
    else()
        set(CURRENT_PLUGIN_SRC "services/${ITEM_LOWER_SUB}")
    endif()
    if(BUILD_DOCS)
        list(APPEND DOCS_LIST "${CMAKE_SOURCE_DIR}/${CURRENT_PLUGIN_SRC}")
    endif()
    if(${ITEM_UPPER})
        add_subdirectory(${CURRENT_PLUGIN_SRC})
        if (${ITEM_UPPER})  # this can change when adding the target
            list(FIND PLUGIN_UNORDERED ${ITEM_LOWER} ITEM_FOUND)
            if(${ITEM_FOUND} GREATER_EQUAL 0)
                list(APPEND PLUGIN_UNORDERED ${ITEM_LOWER})
            endif()
            message("Adding plugins/${ITEM_LOWER}")
        endif()
    endif()
    unset(ITEM_LOWER)
    unset(ITEM_UPPER)
endforeach()

include(src/CMakeLists.txt)

list(SORT FULL_PLUGIN_LIST COMPARE STRING CASE INSENSITIVE ORDER ASCENDING)
set(FULL_PLUGIN_LIST_STRING "\n")
foreach(PLUGIN_NAME IN LISTS FULL_PLUGIN_LIST)
    set(FULL_PLUGIN_LIST_STRING "${FULL_PLUGIN_LIST_STRING}\n  - ${PLUGIN_NAME}")
endforeach()
set(FULL_PLUGIN_LIST_STRING "${FULL_PLUGIN_LIST_STRING}\n")
set(CURRENT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")


# deal with the data file if there is one
if(DATA_FILE)
    string(REGEX MATCH "^http" HAVE_MATCH ${DATA_FILE})
    if(NOT HAVE_MATCH)
        message("Data file appears local")
        set(LOCAL_DATA "${DATA_FILE}")
    elseif(EXISTS "./data.zip")
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

if(BUILD_ZED_CAPTURE)
    add_subdirectory(plugins/zed/capture)
endif()

# set up to install profile files
if(ILLIXR_PROFILE_NAMES)
    foreach(GRP IN LISTS ILLIXR_PROFILE_NAMES)
        install(FILES "${CMAKE_SOURCE_DIR}/profiles/${GRP}.yaml"
                DESTINATION $ENV{HOME}/.illixr/profiles)
        install(FILES "${CMAKE_SOURCE_DIR}/profiles/${GRP}.yaml"
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/illixr/profiles)
    endforeach()
endif()

if(BUILD_DEP_MAP)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        set(DEP_MAP_PRODUCTS "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml ${CMAKE_SOURCE_DIR}/docs/dataflow.dot")
        message(STATUS "Rebuilding dep_map")
        add_custom_target(depmaker ALL
                          BYPRODUCTS "${DEP_MAP_PRODUCTS}"
                          COMMAND ${CMAKE_SOURCE_DIR}/dep_mapper.py ${CMAKE_BINARY_DIR}
                          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        )
        if(USE_AUDIO_PIPELINE)
            add_dependencies(depmaker "Audio_Pipeline")
        endif()
        if(USE_OPENVINS)
            add_dependencies(depmaker "OpenVINS")
        endif()
        if(USE_HAND_TRACKING)
            set(dependencies "${dependencies} ${HT_TARGET_NAME}")
        endif()
        if(USE_ORB_SLAM3)
            set(dependencies "${dependencies} ORB_Slam3")
        endif()
    endif()
endif()

# set up documentaiton build
add_subdirectory(docs)

# give a summary of what will be done
include(cmake/ConfigurationSummary.cmake)

install(FILES "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
        DESTINATION $ENV{HOME}/.illixr/profiles)
install(FILES "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/illixr/profiles)

#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
