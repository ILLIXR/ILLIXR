# CMake module to look for Sophus
# if it is not found then it is downloaded and marked for compilation and install

find_package(Sophus 1.22 QUIET)

if (NOT Sophus_FOUND)
    find_package(fmt REQUIRED)
    EXTERNALPROJECT_ADD(Sophus
            GIT_REPOSITORY https://github.com/strasdat/Sophus.git   # Git repo for source code
            GIT_TAG 1.22.10                                         # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/Sophus                 # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release -DBUILD_SOPHUS_TESTS=OFF -DBUILD_SOPHUS_EXAMPLES=OFF -DCMAKE_INSTALL_LIBDIR=lib
            )
    # set variables for use by modules that depend on this one
    set(Sophus_DEP_STR "Sophus")   # Dependency string for other modules that depend on this one
    set(Sophus_EXTERNAL Yes)       # Mark that this module is being built
endif()
