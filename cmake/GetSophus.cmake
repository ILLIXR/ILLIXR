# CMake module to look for Sophus
# if it is not found then it is downloaded and marked for compilation and install

find_package(Sophus 1.22 QUIET)
list(APPEND EXTERNAL_PROJECTS Sophus)

if (NOT Sophus_FOUND)
    find_package(fmt REQUIRED)
    fetch_git(NAME Sophus
              REPO htps://github.com/strasdat/Sophus.git
              TAG 1.22.10
    )

    set(TEMP_FLAGS ${CMAKE_CXX_FLAGS})
    set(CMAKE_CXX_FLAGS "-L${CMAKE_INSTALL_PREFIX}/lib")
    set(BUILD_SOPHUS_TESTS OFF)
    set(BUILD_SOPHUS_EXAMPLES OFF)
    configure_target(NAME Sophus
                     VERSION 1.22
    )
    unset(BUILD_SOPHUS_TESTS)
    unset(BUILD_SOPHUS_EXAMPLES)
    set(CMAKE_CXX_FLAGS ${TEMP_FLAGS})
    unset(TEMP_FLAGS)

else()
    set(Sophus_VERSION ${Sophus_VERSION} PARENT_SCOPE)
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
