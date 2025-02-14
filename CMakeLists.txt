########################################################################################
#
# This is the install script for ILLIXR
# This will compile the main ILLIXR binary along with any requested plugins.
#
# See the documentation at https://illixr.github.io/ILLIXR/getting_started/
# for complete build instructions.#
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

    find_package(ECM REQUIRED NO_MODULE)
    set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
elseif(WIN32)
    message(FATAL_ERROR "Windows is currently not supported")
elseif(APPLE)
    message(FATAL_ERROR "MacOS is currently not supported")
else()
    message(FATAL_ERROR "Unknown operating system")
endif()

if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" AND EXISTS "/etc/nv_tegra_release")
    set(ON_JETSON_PLATFORM YES)
else()
    set(ON_JETSON_PLATFORM NO)
endif()
if(PACKAGE_VERSION_LABEL)
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}-${PACKAGE_VERSION_LABEL}")
else()
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}")
endif()

include(ExternalProject)

project(ILLIXR VERSION 3.2.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DGLSL_VERSION="330")

# Vulkan flags
# do we only need this if compile with vulkan?  TODO: XYZ
add_compile_definitions(VK_ENABLE_BETA_EXTENSIONS)

set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -Wall -Wextra -rdynamic -Wno-attributes -Wno-unknown-pragmas -fPIC")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -Wno-attributes -Wno-unknown-pragmas -fPIC")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-ggdb -O3 -Wall -Wextra -Wno-attributes -Wno-unknown-pragmas -fPIC")

# needed to generate runtime scripts
set(MONADO_RUNTIME_VK "")
set(MONADO_RUNTIME_GL "")
set(OPENXR_RUNTIME "")
set(OPENXR_PLUGINS "")

# set the CMake search path to include the local files
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake;${CMAKE_MODULE_PATH})
option(COLOR_OUTPUT "Produce color output to the screen" ON)
option(DATA_FILE "The data file to get" ON)
set(DATA_FILE "http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip")

option(BUILD_DOCS "Build documentation" OFF)
option(USE_MONADO "Build for monado use" OFF)
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

set(VISUALIZER "" CACHE STRING "")
set(VISUALIZER_TO_USE "ALL" CACHE INTERNAL "")
if(VISUALIZER)
    string(TOUPPER ${VISUALIZER} VISUALIZER)
    set(VISUALIZER_TO_USE ${VISUALIZER} CACHE INTERNAL "")
endif()

generate_yaml()
check_plugins()

set(CORE_PLUGINS OFF)
set(RT_SLAM_PLUGINS OFF)

if(YAML_FILE)
    read_yaml(${YAML_FILE})
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

set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/lib64/pkgconfig")

link_directories(BEFORE ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib64)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/include)

find_package(PkgConfig REQUIRED)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source build prohibited.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake;${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/share/cmake)

add_definitions(-DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX})
##############
# dependencies
##############
set(EXTERNAL_LIBRARIES "")

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
        set(OpenCV_DEP_STR OpenCV_Viz)
        set(INTERNAL_OPENCV ${OpenCV_DIR})
        set(BUILD_OPENCV Yes)
    endif()
else()
    find_package(OpenCV 4 REQUIRED)
endif()

find_package(GLEW REQUIRED)

pkg_check_modules(glu REQUIRED glu)

find_package(SQLite3 REQUIRED)

find_package(X11 REQUIRED)

pkg_check_modules(gl REQUIRED gl)

find_package(Eigen3 REQUIRED)

#############
# plugin specific dependencies that are used by several plugins
#############

find_package(Git REQUIRED)
if (CMAKE_VERSION VERSION_LESS "3.30.0")
    find_package(Boost REQUIRED COMPONENTS filesystem serialization iostreams)
else()
    find_package(Boost REQUIRED COMPONENTS filesystem serialization iostreams CONFIG)
endif()

find_package(spdlog REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads)

set(SPECIAL_FLAGS "Vulkan")  # these are flags set to enable specific things
# set any plugins that are being built
set(FULL_PLUGIN_LIST "")
set(ILLIXR_COMPILE_FLAGS "")
set(INTERNAL_ENABLED_PLUGINS "")
foreach(ITEM IN LISTS PLUGIN_LIST VISUALIZER_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    string(TOLOWER ${ITEM} ITEM_LOWER)
    list(APPEND FULL_PLUGIN_LIST ${ITEM})
    if(${ITEM_UPPER})
        string(REPLACE "." "/" ITEM_LOWER "${ITEM_LOWER}")
        check_for_external(FILE_NAME "${CMAKE_SOURCE_DIR}/plugins/${ITEM_LOWER}/CMakeLists.txt"
                           EXT_TO_CHECK ${SPECIAL_FLAGS}
                           OUT_FLAGS ILLIXR_COMPILE_FLAGS
        )
    endif()
    unset(ITEM_LOWER)
    unset(ITEM_UPPER)
endforeach()
list(REMOVE_DUPLICATES ILLIXR_COMPILE_FLAGS)
string(REPLACE ";" " " ILLIXR_COMPILE_FLAGS_STR "${ILLIXR_COMPILE_FLAGS}")
foreach(ITEM IN LISTS ILLIXR_COMPILE_FLAGS)
    add_compile_definitions(${ITEM})
    set(${ITEM} ON)
endforeach()

add_subdirectory(utils)

foreach(ITEM IN LISTS PLUGIN_LIST VISUALIZER_LIST)
    string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
    string(TOLOWER ${ITEM} ITEM_LOWER)
    list(APPEND FULL_PLUGIN_LIST ${ITEM})
    if(${ITEM_UPPER})
        string(REPLACE "." "/" ITEM_LOWER_SUB "${ITEM_LOWER}")
        add_subdirectory(plugins/${ITEM_LOWER_SUB})
        if (${ITEM_UPPER})  # this can change when adding the target
            list(APPEND OPENXR_PLUGINS ${ITEM_LOWER})
            message("Adding plugins/${ITEM_LOWER}")
            file(SIZE "${CMAKE_SOURCE_DIR}/plugins/${ITEM_LOWER_SUB}/CMakeLists.txt" F_SIZE)
            if (F_SIZE GREATER_EQUAL 100 AND NOT ${ILLIXR_COMPILE_FLAGS_STR} EQUAL "")
                target_compile_definitions(plugin.${ITEM_LOWER}${ILLIXR_BUILD_SUFFIX} PUBLIC ${ILLIXR_COMPILE_FLAGS_STR})
            endif()
        endif()
    endif()
    unset(ITEM_LOWER)
    unset(ITEM_UPPER)
endforeach()

if (ENABLE_MONADO)
    if (NOT USE_TIMEWARP_GL)
        get_external(Monado_vk)
    elseif (USE_TIMEWARP_GL AND NOT USE_TIMEWARP_VK)

    else()
        get_external(Monado_vk)
    endif()
endif()
include(src/CMakeLists.txt)

list(SORT FULL_PLUGIN_LIST COMPARE STRING CASE INSENSITIVE ORDER ASCENDING)
set(FULL_PLUGIN_LIST_STRING "")
foreach(PLUGIN_NAME IN LISTS FULL_PLUGIN_LIST)
    set(FULL_PLUGIN_LIST_STRING "${FULL_PLUGIN_LIST_STRING}\n- ${PLUGIN_NAME}")
endforeach()
set(CURRENT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")

# set up documentaiton build
add_subdirectory(docs)

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

# give a summary of what will be done
include(cmake/ConfigurationSummary.cmake)

# set up to install profile files
if(ILLIXR_PROFILE_NAMES)
    foreach(GRP IN LISTS ILLIXR_PROFILE_NAMES)
        install(FILES "${CMAKE_SOURCE_DIR}/profiles/${GRP}.yaml"
                DESTINATION $ENV{HOME}/.illixr/profiles)
        install(FILES "${CMAKE_SOURCE_DIR}/profiles/${GRP}.yaml"
                DESTINATION ${CMAKE_INSTALL_PREFIX}/share/illixr/profiles)
    endforeach()
endif()

# reset this, so it is clean for subsequent runs of cmake
set(VISUALIZER "" CACHE STRING "")

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(depmaker ALL
                      BYPRODUCTS "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
                      COMMAND ${CMAKE_SOURCE_DIR}/dep_mapper.py ${CMAKE_BINARY_DIR}
                      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    if(USE_AUDIO_PIPELINE)
        add_dependencies(depmaker "Audio_Pipeline")
    endif()
    if(USE_OPENVINS)
        add_dependencies(depmaker "OpenVINS")
    endif()

endif()
install(FILES "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
        DESTINATION $ENV{HOME}/.illixr/profiles)
install(FILES "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/illixr/profiles)

#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
