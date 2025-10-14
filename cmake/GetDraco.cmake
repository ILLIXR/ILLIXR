cmake_minimum_required(VERSION 3.24)

find_package(draco_illixr QUIET CONFIG)
if (NOT draco_illixr_FOUND)
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "draco should be installed with vcpkg")
    endif()

    fetch_git(NAME Draco_ILLIXR
              REPO https://github.com/ILLIXR/draco_illixr.git
              TAG 4dae9f429fa4c98aab907350de7e8d8c2c878817
    )
    set(DRACO_TRANSCODER_SUPPORTED ON)
    configure_target(Draco_ILLIXR)
    unset(DRACO_TRANSCODER_SUPPORT)
    set(Draco_EXTERNAL Yes)      # Mark that this module is being built
else()
    message(STATUS "  Found draco_illixr, ${draco_illixr_VERSION}")
endif()
