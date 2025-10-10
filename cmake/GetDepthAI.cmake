# CMake module to look for depthai
# if it is not found then it is downloaded and marked for compilation and install

find_package(depthai QUIET)
list(APPEND EXTERNAL_PROJECTS depthai)
if(depthai_FOUND)
    set(depthai_VERSION "${depthai_VERSION}" PARENT_SCOPE)   # set current version
else()
    fetch_git(NAME depthai
              REPO https://github.com/luxonis/depthai-core.git
              TAG v2.29.0
    )
    set(BUILD_SHARED_LIBS ON)
    config_target(NAME depthai)

    #set(DepthAI_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include ${CMAKE_INSTALL_PREFIX}/include/depthai-shared ${CMAKE_INSTALL_PREFIX}/include/depthai-shared/3rdparty ${CMAKE_INSTALL_PREFIX}/include/depthai ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include)
    #set(DepthAI_LIBRARIES depthai-core;depthai-opencv)

    add_custom_target(cleanup_depthai_spdlog
                      COMMAND rm -rf ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include/spdlog
    )
    add_dependencies(cleanup_depthai_spdlog depthai)
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
