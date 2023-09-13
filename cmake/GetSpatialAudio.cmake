# CMake module to look for spatialaudio
# if it is not found then it is downloaded and marked for compilation and install

pkg_check_modules(SPATIALAUDIO QUIET spatialaudio)

if(SPATIALAUDIO_FOUND)
    set(SpatialAudio_VERSION "${SPATIALAUDIO_VERSION}")   # set current version
else()
    EXTERNALPROJECT_ADD(SpatialAudio
            GIT_REPOSITORY https://github.com/ILLIXR/libspatialaudio.git   # Git repo for source code
            GIT_TAG 12a48a20e45d9a7203d49821e2c4f253c8f933b7               # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/spatialaudio                  # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
            )
    # set variables for use by modules that depend on this one
    set(SpatialAudio_DEP_STR "SpatialAudio")   # Dependency string for other modules that depend on this one
    set(SpatialAudio_EXTERNAL Yes)             # Mark that this module is being built
endif()
