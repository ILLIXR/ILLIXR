# CMake module to look for Pangolin
# if it is not found then it is downloaded and marked for compilation and install

find_package(Pangolin 0.6 EXACT QUIET)

if(NOT Pangolin_FOUND)
    EXTERNALPROJECT_ADD(Pangolin
            GIT_REPOSITORY https://github.com/stevenlovegrove/Pangolin.git   # Git repo for source code
            GIT_TAG dd801d244db3a8e27b7fe8020cd751404aa818fd        # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/Pangolin               # the build directory
            # the code needs to be patched to build on some systems
            PATCH_COMMAND ${PROJECT_SOURCE_DIR}/cmake/do_patch.sh -p ${PROJECT_SOURCE_DIR}/cmake/pangolin.patch
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release -DBUILD_PANGOLIN_PYTHON=OFF
            )
    # set variables for use by modules that depend on this one
    set(Pangolin_DEP_STR "Pangolin")   # Dependency string for other modules that depend on this one
    set(Pangolin_EXTERNAL Yes)         # Mark that this module is being built
endif()
