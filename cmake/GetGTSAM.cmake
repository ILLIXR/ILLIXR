# CMake module to look for GTSAM
# if it is not found then it is downloaded and marked for compilation and install

set(GTSAM_CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}\ -DCMAKE_BUILD_TYPE=Release\ -DGTSAM_WITH_TBB=OFF\ -DGTSAM_USE_SYSTEM_EIGEN=ON\ -DGTSAM_POSE3_EXPMAP=ON\ -DGTSAM_ROT3_EXPMAP=ON\ -DGTSAM_BUILD_TESTS=OFF\ -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF\ -DCMAKE_INSTALL_LIBDIR=lib")

# if OpenCV4 is found we use this version of GTSAM
if(USE_V4_CODE)
    find_package(GTSAM 4.3.0 EXACT QUIET)
    set(GTSAM_GIT_TAG "9a7d05459a88c27c65b93ea75b68fa1bc0fc0e4b")
else() # if OpenCV3 is found we use this version of GTSAM
    find_package(GTSAM 3.2 QUIET)
    set(GTSAM_GIT_TAG "3.2.3")
    set(GTSAM_CMAKE_ARGS "${GTSAM_CMAKE_ARGS}\ -DGTSAM_WITH_EIGEN_UNSUPPORTED=ON")
endif()

if(NOT GTSAM_FOUND)
    EXTERNALPROJECT_ADD(GTSAM
            GIT_REPOSITORY https://github.com/ILLIXR/gtsam.git   # Git repo for source code
            GIT_TAG ${GTSAM_GIT_TAG}                             # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/GTSAM               # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS ${GTSAM_CMAKE_ARGS}
            # using a custom install because GTSAM names its libraries strangely
            INSTALL_COMMAND make install && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsamRelease.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam.so && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstableRelease.so ${CMAKE_INSTALL_PREFIX}/lib/libgtsam_unstable.so
            )

    # set variables for use by modules that depend on this one
    set(GTSAM_EXTERNAL Yes)      # Mark that this module is being built
    set(GTSAM_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
    set(GTSAM_DEP_STR "GTSAM")   # Dependency string for other modules that depend on this one
endif()

set(GTSAM_LIBRARIES gtsam;gtsam_unstable)
