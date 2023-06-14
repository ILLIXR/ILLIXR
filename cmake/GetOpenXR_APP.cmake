# module to download, build and install the OpenXR_App ILLIXR plugin

find_package(SDL2 REQUIRED)

EXTERNALPROJECT_ADD(OpenXR_App
        GIT_REPOSITORY https://gitlab.freedesktop.org/monado/demos/openxr-simple-example.git   # Git repo for source code
        GIT_TAG 94f1a764dd736b23657ff01464ec1518771e8cdc            # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${CMAKE_BINARY_DIR}/_deps/OpenXR_APP                 # the build directory
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        )
