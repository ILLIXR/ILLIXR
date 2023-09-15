# module to download, build and install the OpenXR_App ILLIXR plugin

find_package(SDL2 REQUIRED)

EXTERNALPROJECT_ADD(OpenXR_App
        GIT_REPOSITORY https://github.com/ILLIXR/Monado_OpenXR_Simple_Example.git   # Git repo for source code
        GIT_TAG ba944eb049337669d0e24d24619a03915eed31d6            # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${CMAKE_BINARY_DIR}/_deps/OpenXR_APP                 # the build directory
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        )
set(OpenXR_APP_EXTERNAL Yes)
set(OPENXR_RUNTIME "${CMAKE_INSTALL_PREFIX}/bin/openxr-example" PARENT_SCOPE)
