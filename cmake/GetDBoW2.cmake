# CMake module to look for DBoW2
# if it is not found then it is downloaded and marked for compilation and install

find_package(DBoW2_OS3 QUIET)

if(DBoW2_OS3_LIBRARIES)
    set(DBoW2_VERSION "OS3" PARENT_SCOPE)   # set current version (no known version in this case)
else()
    FetchContent_Declare(DBoW2_OS3_ext
                         GIT_REPOSITORY https://github.com/ILLIXR/DBoW2_OS3.git # Git repo for source code
                         GIT_TAG 3cb52aa1162cd07354f75512454b0dea75cce7c1       # sha5 hash for specific commit to pull (if there is no specific tag to use)
                         OVERRIDE_FIND_PACKAGE
    )
    set(CMAKE_BUILD_TYPE Release)
    FetchContent_MakeAvailable(DBoW2_OS3_ext)
    set(CMAKE_BUILD_TYPE ${ILLIXR_BUILD_TYPE})
endif()
