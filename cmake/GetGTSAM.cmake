# CMake module to look for GTSAM
# if it is not found then it is downloaded and marked for compilation and install

find_package(GTSAM 4.3.0 QUIET EXACT)
find_package(GTSAM_UNSTABLE 4.3.0 QUIET EXACT)

if(NOT GTSAM_FOUND)
    message("GTSAM NOT FOUND - building from source")
    EXTERNALPROJECT_ADD(GTSAM_EXT
            GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git   # Git repo for source code
            GIT_TAG 135f09fe08f749596a03d4d018387f4590f826c1     # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/GTSAM               # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DGTSAM_WITH_TBB=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON -DGTSAM_POSE3_EXPMAP=ON -DGTSAM_ROT3_EXPMAP=ON -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE}
            )

    # set variables for use by modules that depend on this one
    set(GTSAM_EXTERNAL Yes)      # Mark that this module is being built
    set(GTSAM_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
    set(GTSAM_DEP_STR "GTSAM_EXT")   # Dependency string for other modules that depend on this one
else()
    set(GTSAM_VERSION ${GTSAM_VERSION} PARENT_SCOPE)
endif()

set(GTSAM_LIBRARIES gtsam;gtsam_unstable)
