# CMake module to look for DBoW2
# if it is not found then it is downloaded and marked for compilation and install

find_package(DBoW2_OS3 QUIET)

set(DBOW_CMAKE_ARGS "")

if(DBoW2_OS3_LIBRARIES)
    set(DBoW2_VERSION "OS3" PARENT_SCOPE)   # set current version (no known version in this case)
else()
    EXTERNALPROJECT_ADD(DBoW2_OS3
            GIT_REPOSITORY https://github.com/ILLIXR/DBoW2_OS3.git # Git repo for source code
            GIT_TAG 3cb52aa1162cd07354f75512454b0dea75cce7c1       # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/DBoW2_OS3             # the build directory
            #arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release ${DBOW_CMAKE_ARGS} -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE} -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            )
    set(DBoW2_DEP_STR "DBoW2_OS3")   # Dependency string for other modules that depend on this one
    set(DBoW2_EXTERNAL Yes)      # Mark that this module is being built
endif()
