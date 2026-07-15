# module to download, build and install the OpenXR_App ILLIXR plugin

find_package(OpenXR REQUIRED)
find_package(SDL2 REQUIRED)
list(APPEND EXTERNAL_PROJECTS OpenXR)

find_program(OPENXR_EXAMPLE_FOUND
             openxr-example
             HINTS ${CMAKE_INSTALL_PREFIX}/bin
)
if (OPENXR_EXAMPLE_FOUND)
    set(OpenXR_VERSION ${OpenXR_VERSION} PARENT_SCOPE)
else()
    message(STATUS "Looking for openxr-example - not found\n      will build from source")
    fetch_git(NAME OpenXR_App
              REPO https://github.com/ILLIXR/Monado_OpenXR_Simple_Example.git
              TAG ba944eb049337669d0e24d24619a03915eed31d6
    )

    configure_target(NAME OpenXR_App
                     NO_FIND
    )
endif ()
set(OPENXR_RUNTIME "${CMAKE_INSTALL_PREFIX}/bin/openxr-example" PARENT_SCOPE)
set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
