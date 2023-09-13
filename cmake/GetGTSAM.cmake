# CMake module to look for GTSAM
# if it is not found then it is downloaded and marked for compilation and install

find_package(GTSAM 4.3.0 EXACT QUIET)

if(NOT GTSAM_FOUND)
    EXTERNALPROJECT_ADD(GTSAM_EXT
            GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git   # Git repo for source code
            GIT_TAG d39389fec49a5f65db3c7f46537bb820b5bf80ed     # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/GTSAM               # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DGTSAM_WITH_TBB=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON -DGTSAM_POSE3_EXPMAP=ON -DGTSAM_ROT3_EXPMAP=ON -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF -DCMAKE_INSTALL_LIBDIR=lib
            )

    # set variables for use by modules that depend on this one
    set(GTSAM_EXTERNAL Yes)      # Mark that this module is being built
    set(GTSAM_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
    set(GTSAM_DEP_STR "GTSAM_EXT")   # Dependency string for other modules that depend on this one
endif()

set(GTSAM_LIBRARIES gtsam;gtsam_unstable)
