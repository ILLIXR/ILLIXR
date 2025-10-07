# CMake module to look for depthai
# if it is not found then it is downloaded and marked for compilation and install

find_package(depthai QUIET)

if(depthai_FOUND)
    set(DepthAI_VERSION "${depthai_VERSION}" PARENT_SCOPE)   # set current version
else()
    FetchContent_Declare(DepthAI_ext
                         GIT_REPOSITORY https://github.com/luxonis/depthai-core.git   # Git repo for source code
                         GIT_TAG v2.29.0                                              # sha5 hash for specific commit to pull (if there is no specific tag to use)
                         OVERRIDE_FIND_PACKAGE
    )
    set(CMAKE_BUILD_TYPE Release)
    set(BUILD_SHARED_LIBS ON)
    set(CMAKE_INSTALL_LIBDIR lib)

    #set(DepthAI_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include ${CMAKE_INSTALL_PREFIX}/include/depthai-shared ${CMAKE_INSTALL_PREFIX}/include/depthai-shared/3rdparty ${CMAKE_INSTALL_PREFIX}/include/depthai ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include)
    #set(DepthAI_LIBRARIES depthai-core;depthai-opencv)

    add_custom_target(cleanup_depthai_spdlog
                      COMMAND rm -rf ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include/spdlog
    )

    add_dependencies(cleanup_depthai_spdlog DepthAI_ext)
endif()
