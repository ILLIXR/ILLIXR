cmake_minimum_required(VERSION 3.24)

find_package(draco_illixr QUIET CONFIG)
if (NOT draco_illixr_FOUND)
    message(STATUS "Draco not found, downloading.")
    FetchContent_Declare(Draco_ILLIXR
                         GIT_REPOSITORY https://github.com/ILLIXR/draco_illixr.git
                         GIT_TAG 4dae9f429fa4c98aab907350de7e8d8c2c878817
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
