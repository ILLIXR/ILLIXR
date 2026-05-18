# CMake module to look for Sophus
# if it is not found then it is downloaded and marked for compilation and install

find_package(Sophus 1.22 QUIET)

if (Sophus_FOUND)
    set(Sophus_VERSION ${Sophus_VERSION} PARENT_SCOPE)
    message(STATUS "  Found Sophus, ${Sophus_VERSION}")
else()
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "sophus should be installed with vcpkg")
    endif()
    find_package(fmt REQUIRED)

    fetch_git(NAME Sophus
              REPO https://github.com/strasdat/Sophus.git
              TAG 1.22.10
              PATCH
              NO_OVERRIDE
    )

    #-DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib"
    set(BUILD_SOPHUS_EXAMPLES OFF)
    set(SOPHUS_INSTALL ON CACHE BOOL "" FORCE)
    configure_target(NAME Sophus
                     NO_FIND
    )
    set(Sophus_DIR ${Sophus_BINARY_DIR} CACHE PATH "" FORCE)
    unset(BUILD_SOPHUS_EXAMPLES)
    unset(SOPHUS_INSTALL)
endif()
