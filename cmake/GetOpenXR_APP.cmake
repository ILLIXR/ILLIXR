find_package(SDL2 REQUIRED)

EXTERNALPROJECT_ADD(OpenXR_App
        GIT_REPOSITORY https://gitlab.freedesktop.org/monado/demos/openxr-simple-example.git
        GIT_TAG 94f1a764dd736b23657ff01464ec1518771e8cdc
        PREFIX ${CMAKE_BINARY_DIR}/_deps/OpenXR_APP
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        )
