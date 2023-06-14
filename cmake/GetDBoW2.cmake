# CMake module to look for DBoW2
# if it is not found then it is downloaded and marked for compilation and install

find_package(DBoW2 QUIET)

set(DBOW_CMAKE_ARGS "")

# if building on CentOS make sure we use the correct OpenCV
if(HAVE_CENTOS)
    set(DBOW_CMAKE_ARGS "-DOpenCV_DIR=${OpenCV_DIR}")
endif()

if(DBoW2_LIBRARIES)
    set(DBoW2_VERSION "0.0")   # set current version (no known version in this case)
else()
    EXTERNALPROJECT_ADD(DBoW2
            GIT_REPOSITORY https://github.com/dorian3d/DBoW2.git   # Git repo for source code
            GIT_TAG 3924753db6145f12618e7de09b7e6b258db93c6e       # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/DBoW2                 # the build directory
            DEPENDS ${OpenCV_DEP_STR}                              # dependencies of this module
            #arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release ${DBOW_CMAKE_ARGS}
            )
    set(DBoW2_DEP_STR "DBoW2")   # Dependency string for other modules that depend on this one
    set(DBoW2_EXTERNAL Yes)      # Mark that this module is being built
endif()
