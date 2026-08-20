cmake_minimum_required(VERSION 3.24)

find_package(draco_illixr QUIET CONFIG)
if (draco_illixr_FOUND)
    message(STATUS "  Found draco_illixr, ${draco_illixr_VERSION}")
else()
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "draco should be installed with vcpkg")
    endif()

    fetch_git(NAME Draco_ILLIXR
              REPO https://github.com/ILLIXR/draco_illixr.git
              TAG edbe455c1aa2e65967907691d9e37a94eaa949d5
    )
    set(DRACO_TRANSCODER_SUPPORTED ON)
    configure_target(NAME Draco_ILLIXR)
    unset(DRACO_TRANSCODER_SUPPORT)
    set(Draco_EXTERNAL Yes)      # Mark that this module is being built

endif()
