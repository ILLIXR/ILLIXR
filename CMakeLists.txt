########################################################################################
#
# This is the install script for ILLIXR
# This will compile the main ILLIXR binary along with any requested plugins.
#
# See the documentation at https://illixr.github.io/ILLIXR/getting_started/
# for complete build instructions.
#
########################################################################################

cmake_minimum_required(VERSION 3.22.1)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

if(UNIX AND NOT BUILD_ANDROID)
    # since several plugins now require clang to compile themselves or their dependencies
    # clang is now a required dependency
    find_program(CLANG_EXE clang REQUIRED)
    find_program(CLANG_CXX_EXE clang++ REQUIRED NAMES clang-cpp clang++)
    set(CMAKE_C_COMPILER ${CLANG_EXE} CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER ${CLANG_CXX_EXE} CACHE FILEPATH "" FORCE)
endif()

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source build prohibited.")
endif("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

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
set(PACKAGE_STRING "ILLIXR 4.1.0")
set(PACKAGE_VERSION_MAJOR "4")
set(PACKAGE_VERSION_MINOR "1")
set(PACKAGE_VERSION_PATCH "0")
set(PACKAGE_VERSION_LABEL "")

set(GPL_LICENSED_CODE "openvins;orb_slam3;offload_rendering_server;offload_rendering_client")
set(LGPL_LICENSED_CODE "audio_pipeline")

set(DEPENDENCY_BUILD_SUFFIX "")
if(WIN32 OR MSVC)
    if(NOT CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR "For windows builds the CMAKE_TOOLCHAIN_FILE must be given, pointing to the toolchain file of vcpkg.")
    endif()
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ports")
    add_compile_definitions("-DWIN_OS")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    set(VCPKG_TARGET_TRIPLET "x64-windows")
    set(VCPKG_POLICY_ALLOW_EXES_IN_BIN enabled)
    set(DEPENDENCY_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    set(DEPENDENCY_BUILD_SUFFIX "$<IF:$<CONFIG:Debug>,d, >")
elseif(APPLE)
    message(FATAL_ERROR "MacOS is currently not supported")
elseif(BUILD_ANDROID)
    message(STATUS "Android build enabled")
elseif(NOT UNIX)
    message(FATAL_ERROR "Unknown operating system")
else()
    set(DEPENDENCY_BUILD_TYPE Release)
endif()

set(GPL_LICENSED_CODE "openvins;orb_slam3;offload_rendering_server;offload_rendering_client")
set(LGPL_LICENSED_CODE "audio_pipeline")

if(PACKAGE_VERSION_LABEL)
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}-${PACKAGE_VERSION_LABEL}")
else()
    set(PACKAGE_VERSION "${PACKAGE_VERSION_MAJOR}.${PACKAGE_VERSION_MINOR}.${PACKAGE_VERSION_PATCH}")
endif()

include(ExternalProject)
include(FetchContent)
include(CMakeDependentOption)

# check for the Jetson platform
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" AND EXISTS "/etc/nv_tegra_release")
    set(ON_JETSON_PLATFORM YES)
else()
    set(ON_JETSON_PLATFORM NO)
endif()

# needed to generate runtime scripts
set(OPENXR_RUNTIME "")

# set the CMake search path to include the local files
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
if(NOT BUILD_ANDROID)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}")
endif()
option(COLOR_OUTPUT "Produce color output to the screen" ON)
option(BUILD_OXR_INTERFACE "Build the OpenXR interface (hand tracking only)" OFF)
if(BUILD_OXR_INTERFACE AND BUILD_ANDROID)
    set(BUILD_OXR_INTERFACE OFF CACHE BOOL "Build the OpenXR interface (hand tracking only)" FORCE)
endif()
cmake_dependent_option(BUILD_OXR_TEST "Build the  test for the OpenXr interface (hand tracking only)" OFF BUILD_OXR_INTERFACE OFF)
option(BUILD_ZED_CAPTURE "Build the ZED capture program" OFF)
if(BUILD_ZED_CAPTURE AND BUILD_ANDROID)
    set(BUILD_ZED_CAPTURE OFF CACHE BOOL "Build the ZED capture program" FORCE)
endif()

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
if(NOT BUILD_ANDROID)
    option(BUILD_PARALLEL_LEVEL "Number of parallel build jobs")
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
endif()

option(ENABLE_MONADO "Build monado package" OFF)

include(cmake/HelperFunctions.cmake)

generate_yaml()
check_plugins()

if(YAML_FILE)
    read_yaml(${YAML_FILE})
    string(REGEX MATCH "/all.yaml" MATCH_FOUND ${YAML_FILE})
    if (${YAML_FILE} STREQUAL "all.yaml" OR MATCH_FOUND)
        set(BUILD_DEP_MAP OFF)
    endif()
    if(LOCAL_BUILD_FLAGS)
        add_compile_definitions("${LOCAL_BUILD_FLAGS}")
        string(REPLACE " " ";" TEMP_BUILD_FLAGS "${LOCAL_BUILD_FLAGS}")
        foreach(B_FLAG IN LISTS TEMP_BUILD_FLAGS)
            set(${B_FLAG} ON)
        endforeach()
    endif()
endif()

set(ILLIXR_SOURCE_DIR ${CMAKE_SOURCE_DIR})

#################################

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
add_definitions(-DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX})

set(ILLIXR_BUILD_TYPE ${CMAKE_BUILD_TYPE})
set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)
if(NOT BUILD_ANDROID)
    set(ENV{PKG_CONFIG_PATH} "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig:${CMAKE_INSTALL_PREFIX}/lib64/pkgconfig")
endif()

if(BUILD_DOCS)
    set(DOCS_LIST "${CMAKE_SOURCE_DIR}/include/illixr;${CMAKE_SOURCE_DIR}/include/illixr/data_format;${CMAKE_SOURCE_DIR}/include/illixr/data_format/poses;${CMAKE_SOURCE_DIR}/include/illixr/data_format/serialization;${CMAKE_SOURCE_DIR}/include/illixr/network;${CMAKE_SOURCE_DIR}/include/illixr/vk;${CMAKE_SOURCE_DIR}/plugins/hand_tracking/viewer;${CMAKE_SOURCE_DIR}/plugins/zed/data_injection;${CMAKE_SOURCE_DIR}/plugins/zed/capture;${CMAKE_SOURCE_DIR}/src;${CMAKE_SOURCE_DIR}/src/display;${CMAKE_SOURCE_DIR}/utils")
    set(DOCS_EXCLUDE_LIST "${CMAKE_SOURCE_DIR}/include/illixr/vk/vk_extension_request.hpp")
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

if(WIN32 OR MSVC)
    # define the needed packages
    set(VCPKG_MANIFEST_FEATURES "")
    foreach(ITEM IN LISTS PLUGIN_LIST)
        string(TOUPPER "USE_${ITEM}" ITEM_UPPER)
        if(${ITEM_UPPER})
            string(TOLOWER ${ITEM} ITEM_LOWER)
            string(REPLACE "." "-" ITEM_LOWER_TMP ${ITEM_LOWER})
            string(REPLACE "_" "-" ITEM_LOWER_REP ${ITEM_LOWER_TMP})
            list(APPEND VCPKG_MANIFEST_FEATURES "plugin-${ITEM_LOWER_REP}")
            unset(ITEM_LOWER)
        endif()
        unset(ITEM_UPPER)
    endforeach()
endif()

set(ILLIXR_VERSION 4.1.0)
if(BUILD_ANDROID)
    project(ILLIXR VERSION ${ILLIXR_VERSION} LANGUAGES C CXX)
else()
    project(ILLIXR VERSION ${ILLIXR_VERSION} LANGUAGES CXX)
endif()

if(WIN32 OR MSVC)
    if(CMAKE_BUILD_TYPE MATCHES Debug)
        set(MSVC_RUNTIME_FLAG "/MDd")
    else()
        set(MSVC_RUNTIME_FLAG "/MD")
    endif()

    # Set this so Boost won't override it
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # CRITICAL: Prevent it from affecting CUDA
    set(CMAKE_CUDA_RUNTIME_LIBRARY "None" CACHE STRING "" FORCE)

    # Ensure C++ runtime is linked
    if(CMAKE_BUILD_TYPE MATCHES Debug)
        link_libraries(vcruntimed.lib msvcrtd.lib)
    else()
        link_libraries(vcruntime.lib msvcrt.lib)
    endif()
    # Remove conflicting static runtime libraries
    add_link_options(/NODEFAULTLIB:libcmt.lib /NODEFAULTLIB:libcmtd.lib)

    set(CMAKE_CXX_FLAGS "/EHsc /W3 ${CMAKE_CXX_FLAGS}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "/EHsc /W3 ${CMAKE_CXX_FLAGS_DEBUG}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "/EHsc /W3 ${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)

    # Set CUDA flags manually
    if(CMAKE_BUILD_TYPE MATCHES Debug)
        set(RT_FLAG "/MDd")
    else()
        set(RT_FLAG "/MD")
    endif()

    set(CMAKE_CUDA_FLAGS "-D_WINDOWS --expt-relaxed-constexpr --use_fast_math -Xcompiler=/W3 -Xcompiler=/EHsc -Xcompiler=${RT_FLAG} -Xcompiler=/wd4005 -Xcompiler=/DNOMINMAX -Xcompiler=/DWIN32_LEAN_AND_MEAN" CACHE STRING "" FORCE)
    set(CMAKE_CUDA_FLAGS_DEBUG "-G -Xcompiler=/Zi -Xcompiler=/Od" CACHE STRING "" FORCE)
    set(CMAKE_CUDA_FLAGS_RELEASE "-O3 -Xcompiler=/O2" CACHE STRING "" FORCE)

    message(STATUS "CMAKE_CUDA_RUNTIME_LIBRARY = ${CMAKE_CUDA_RUNTIME_LIBRARY}")
elseif(ANDROID)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID)
    add_definitions(-DGLSL_VERSION="330")

    # Vulkan flags
    add_compile_definitions(VK_ENABLE_BETA_EXTENSIONS)
endif()

if(WIN32 OR MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Od /MDd")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "/DEBUG /STACK:8388608")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /MD /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /O2")
elseif(ANDROID)
    # Device-specific optimizations (optional)
    if(NOT DEFINED ANDROID_DEVICE_TYPE)
        set(ANDROID_DEVICE_TYPE "generic")
    endif()

    if(ANDROID_DEVICE_TYPE STREQUAL "quest3")
        # Quest 3: Snapdragon XR2 Gen 2 (Cortex-A78 + Cortex-A55)
        set(DEVICE_COMPILE_FLAGS "-march=armv8.2-a -mtune=cortex-a78")
        set(ANDROID_ARM_NEON TRUE)
    elseif(ANDROID_DEVICE_TYPE STREQUAL "quest2")
        # Quest 2: Snapdragon XR2 (Cortex-A77 + Cortex-A55)
        set(DEVICE_COMPILE_FLAGS "-march=armv8.2-a -mtune=cortex-a77")
        set(ANDROID_ARM_NEON TRUE)
    elseif(ANDROID_DEVICE_TYPE STREQUAL "phone")
        # Generic modern phone (ARMv8, NEON)
        set(DEVICE_COMPILE_FLAGS "-march=armv8-a")
        set(ANDROID_ARM_NEON TRUE)
    else()
        # Generic: no specific tuning, maximum compatibility
        set(DEVICE_COMPILE_FLAGS "")
        if(ANDROID_ABI STREQUAL "arm64-v8a" OR ANDROID_ABI STREQUAL "armeabi-v7a")
            set(ANDROID_ARM_NEON TRUE)
        endif()
    endif()

    if(DEVICE_COMPILE_FLAGS)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${DEVICE_COMPILE_FLAGS}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${DEVICE_COMPILE_FLAGS}")
    endif()

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffast-math -fno-finite-math-only")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math -fno-finite-math-only")

    set(VK_USE_PLATFORM_ANDROID_KHR TRUE)
    if(NOT THIRD_PARTY_DIR)
        message(FATAL_ERROR "The THIRD_PARTY_DIR must be specified")
    endif()

    set(OpenCV_DIR "${THIRD_PARTY_DIR}/sdk/native/jni" CACHE PATH "Path to OpenCV cmake configuration files")
    set(Boost_DIR "${THIRD_PARTY_DIR}/lib/${ANDROID_ABI}/cmake/Boost-1.72.0" CACHE PATH "Path to Boost cmake configuration files")
    set(protobuf_android_DIR "${THIRD_PARTY_DIR}/lib/${ANDROID_ABI}/cmake/protobuf_android" CACHE PATH "Path to protobuf configuration files")
    set(spdlog_android_DIR "${THIRD_PARTY_DIR}/lib/${ANDROID_ABI}/cmake/spdlog_android" CACHE PATH "Path to spdlog cmake configuration files")
    set(yaml-cpp_android_DIR "${THIRD_PARTY_DIR}/lib/${ANDROID_ABI}/cmake/yaml-cpp_android" CACHE PATH "Path to spdlog cmake configuration files")
    set(Boost_USE_STATIC_LIBS ON)
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-ggdb -Wall -Wextra -Wno-attributes -Wno-unknown-pragmas -fPIC")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -Wno-attributes -Wno-unknown-pragmas -fPIC")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-ggdb -O3 -Wall -Wextra -Wno-attributes -Wno-unknown-pragmas -fPIC")
endif()

##################################

link_directories(BEFORE ${CMAKE_INSTALL_PREFIX}/lib ${CMAKE_INSTALL_PREFIX}/lib64)
include_directories(BEFORE ${CMAKE_SOURCE_DIR}/include)

if(WIN32 OR MSVC)
    set(PKG_CONFIG_EXECUTABLE "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/tools/pkgconf/pkgconf.exe")
    find_package(PkgConfig REQUIRED)
elseif(NOT ANDROID)
    find_package(PkgConfig REQUIRED)
endif()

if(NOT ANDROID)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${PROJECT_SOURCE_DIR}/cmake;${CMAKE_INSTALL_PREFIX}/lib/cmake;${CMAKE_INSTALL_PREFIX}/share/cmake)
endif()

##############
# dependencies
##############
set(EXTERNAL_LIBRARIES "")

if(ANDROID)
    find_package(OpenCV 4.5.5 REQUIRED CONFIG COMPONENTS opencv_java)
    include_directories(${OpenCV_INCLUDE_DIRS})
else()
    find_package(OpenCV 4 REQUIRED)
    find_package(SQLite3 REQUIRED)
endif()

if(ANDROID)
    find_package(Eigen3 REQUIRED)
else()
    find_package(Eigen3 REQUIRED CONFIG)
endif()

#############
# plugin specific dependencies that are used by several plugins
#############

if(WIN32 OR MSVC)
    find_package(OpenGL REQUIRED)
    pkg_check_modules(X11 REQUIRED)
    find_package(Vulkan REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(glslang REQUIRED)
elseif(NOT ANDROID)
    find_package(X11 REQUIRED)
    pkg_check_modules(gl REQUIRED gl)
    pkg_check_modules(glu REQUIRED glu)
    get_external(Vulkan)
endif()

if(NOT ANDROID)
    find_package(GLEW REQUIRED)
    find_package(glfw3 REQUIRED CONFIG)
endif()

find_package(Git REQUIRED)

if(WIN32 OR MSVC)
    set(Boost_USE_STATIC_LIBS ON)
    add_definitions(-DBOOST_ALL_NO_LIB)
    add_definitions(-DBOOST_SERIALIZATION_DYN_LINK)
endif()

if(ANDROID)
    find_package(Boost 1.72 REQUIRED CONFIG COMPONENTS chrono thread filesystem atomic iostreams serialization)
    include_directories(${Boost_INCLUDE_DIRS})
elseif (CMAKE_VERSION VERSION_LESS "3.30.0")
    find_package(Boost REQUIRED COMPONENTS filesystem serialization iostreams)
else()
    find_package(Boost REQUIRED COMPONENTS filesystem serialization iostreams CONFIG)
endif()

if(ANDROID)
    find_program(protoc_exe protoc HINTS /usr/bin REQUIRED)
    add_executable(protobuf::protoc IMPORTED)
    set_property(TARGET protobuf::protoc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
    set_target_properties(protobuf::protoc PROPERTIES IMPORTED_LOCATION_RELEASE ${protoc_exe})
    find_package(protobuf_android REQUIRED CONFIG)

    find_package(spdlog_android 1.15.3 REQUIRED)
else()
    find_package(spdlog REQUIRED CONFIG)
endif()
set(EXTERNAL_PROJECTS "")
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

# set any plugins that are being built
set(FULL_PLUGIN_LIST "")
if(ANDROID)
    set(ANDROID_PLUGIN_LIST "")
endif()

add_subdirectory(utils)
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
            if(${ITEM_FOUND} EQUAL -1)
                list(APPEND PLUGIN_UNORDERED ${ITEM_LOWER})
            endif()
            message("Adding plugins/${ITEM_LOWER}")
            if(ANDROID)
                list(APPEND ANDROID_PLUGIN_LIST ${ITEM_LOWER})
            endif()
        endif()
    endif()
    unset(ITEM_LOWER)
    unset(ITEM_UPPER)
endforeach()

if(ANDROID)
    # build native_app_glue as a static lib
    add_library(native_app_glue STATIC
                ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
    )

    add_definitions(-DVK_USE_PLATFORM_ANDROID_KHR=1)

    # now build app's shared lib
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -Wall -Werror")

    # Export ANativeActivity_onCreate(),
    # Refer to: https://github.com/android-ndk/ndk/issues/381.
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}  -latomic -u ANativeActivity_onCreate -v -fvisibility=hidden")
endif()

include(src/CMakeLists.txt)

list(SORT FULL_PLUGIN_LIST COMPARE STRING CASE INSENSITIVE ORDER ASCENDING)
set(FULL_PLUGIN_LIST_STRING "\n")
foreach(PLUGIN_NAME IN LISTS FULL_PLUGIN_LIST)
    set(FULL_PLUGIN_LIST_STRING "${FULL_PLUGIN_LIST_STRING}\n  - ${PLUGIN_NAME}")
endforeach()
set(FULL_PLUGIN_LIST_STRING "${FULL_PLUGIN_LIST_STRING}\n")
set(CURRENT_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "")

if(BUILD_ZED_CAPTURE)
    add_subdirectory(plugins/zed/capture)
endif()

if(NOT ANDROID)
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
endif()

# set up documentaiton build
add_subdirectory(docs)

# give a summary of what will be done
include(cmake/ConfigurationSummary.cmake)

if(NOT ANDROID)
    install(FILES "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
            DESTINATION $ENV{HOME}/.illixr/profiles)
    install(FILES "${CMAKE_SOURCE_DIR}/plugins/plugin_deps.yaml"
            DESTINATION ${CMAKE_INSTALL_PREFIX}/share/illixr/profiles)
endif()
#get_cmake_property(_variableNames VARIABLES)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()
