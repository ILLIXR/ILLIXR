# CMake module to look for DBoW2
# if it is not found then it is downloaded and marked for compilation and install

find_package(DBoW2_OS3 QUIET)
list(APPEND EXTERNAL_PROJECTS DBoW2_OS3)

if(DBoW2_OS3_LIBRARIES)
    set(DBoW2_VERSION "OS3" PARENT_SCOPE)   # set current version (no known version in this case)
else()
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "DBoW2 should be installed with vcpkg")
    endif()
    fetch_git(NAME DBoW2_OS3
              REPO https://github.com/ILLIXR/DBoW2_OS3.git
              TAG 3cb52aa1162cd07354f75512454b0dea75cce7c1
    )
    configure_target(NAME DBoW2_OS3)
endif()
set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
