# CMake module to look for KimeraRPGO
# if it is not found then it is downloaded and marked for compilation and install
find_package(KimeraRPGO 1.0 QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)
if(NOT KimeraRPGO_FOUND)
    # if OpenCV4 was found we use this version of KimeraRPGO
    if(USE_V4_CODE)
        ExternalProject_Add(KimeraRPGO
                GIT_REPOSITORY https://github.com/MIT-SPARK/Kimera-RPGO.git   # Git repo for source code
                GIT_TAG 3718a6f29dc1de5c2966fcbd679eee27a7933a68              # sha5 hash for specific commit to pull (if there is no specific tag to use)
                DEPENDS ${GTSAM_DEP_STR}                                      # dependencies of this module
                PREFIX ${CMAKE_BINARY_DIR}/_deps/kimerarpgo                   # the build directory
                # arguments to pass to CMake
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release
                )
    else() # if OpenCV3 was found we use this version
        ExternalProject_Add(KimeraRPGO
                URL https://github.com/MIT-SPARK/Kimera-RPGO/archive/refs/tags/dec-2020.tar.gz   # source tarball to use
                URL_HASH MD5=26ac6e1525523b88d555e61ca74da34b                                    # md5sum hash of the tarball
                DEPENDS ${GTSAM_DEP_STR}                                                         # dependencies of this module
                PREFIX ${CMAKE_BINARY_DIR}/_deps/kimerarpgo                                      # the build directory
                # arguments to pass to CMake
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release
                )
    endif()
        # set variables for use by modules that depend on this one
    set(KimeraRPGO_DEP_STR "KimeraRPGO")   # Dependency string for other modules that depend on this one
    set(KimeraRPGO_EXTERNAL Yes)           # Mark that this module is being built
endif()