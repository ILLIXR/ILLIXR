# CMake module to look for opengv
# if it is not found then it is downloaded and marked for compilation and install

find_package(opengv 1.0 QUIET)

if(NOT opengv_FOUND)
    EXTERNALPROJECT_ADD(opengv
            GIT_REPOSITORY https://github.com/laurentkneip/opengv.git   # Git repo for source code
            GIT_TAG 91f4b19c73450833a40e463ad3648aae80b3a7f3            # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/opengv                     # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
            )
    # set variables for use by modules that depend on this one
    set(opengv_DEP_STR "opengv")   # Dependency string for other modules that depend on this one
    set(opengv_EXTERNAL Yes)       # Mark that this module is being built
endif()