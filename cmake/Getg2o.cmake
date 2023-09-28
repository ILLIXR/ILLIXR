# CMake module to look for g2o
# if it is not found then it is downloaded and marked for compilation and install

find_package(g2o 1.0 QUIET)
if (NOT g2o_FOUND)
    EXTERNALPROJECT_ADD(g2o
            GIT_REPOSITORY https://github.com/RainerKuemmerle/g2o.git   # Git repo for source code
            GIT_TAG 20230223_git                                        # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/g2o                        # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release
            )
    # set variables for use by modules that depend on this one
    set(g2o_DEP_STR "g2o")     # Dependency string for other modules that depend on this one
    set(g2o_EXTERNAL YES)      # Mark that this module is being built
endif()
