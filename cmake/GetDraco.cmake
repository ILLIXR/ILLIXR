cmake_minimum_required(VERSION 3.24)

find_package(draco_illixr QUIET CONFIG)
if (NOT draco_illixr_FOUND)
    message(STATUS "Draco not found, downloading.")
    FetchContent_Declare(Draco_ILLIXR
                         GIT_REPOSITORY https://github.com/ILLIXR/draco.git
                         GIT_TAG bba2a71ae3d46631a3b6d969e60730d570e904aa
                         OVERRIDE_FIND_PACKAGE
    )
    set(TEMP_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    set(DRACO_TRANSCODER_SUPPORTED ON)
    set(CMAKE_BUILD_TYPE=Release)
    FetchContent_MakeAvailable(Draco_ILLIXR)
    set(CMAKE_BUILD_TYPE ${TEMP_BUILD_TYPE})
    unset(DRACO_TRANSCODER_SUPPORT)
    set(Draco_EXTERNAL Yes)      # Mark that this module is being built
else()
    message(STATUS "  Found draco_illixr, ${draco_illixr_VERSION}")
endif()
