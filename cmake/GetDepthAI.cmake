# CMake module to look for depthai
# if it is not found then it is downloaded and marked for compilation and install

find_package(depthai QUIET)
if(depthai_FOUND)
    set(DepthAI_VERSION "${depthai_VERSION}" PARENT_SCOPE)
    message(STATUS "  Found depthai, ${DepthAI_VERSION}")
else()
    fetch_git(NAME DepthAI_ext
              REPO https://github.com/luxonis/depthai-core.git   # Git repo for source code
              TAG v2.29.0                                              # sha5 hash for specific commit to pull (if there is no specific tag to use)
    )

    if(NOT BUILD_SHARED_LIBS)
        set(BUILD_SHARED_LIBS ON)
        set(SWITCH_BACK ON)
    endif()
    configure_target(NAME DepthAI_ext)
    if (SWITCH_BACK)
        set(BUILD_SHARED_LIBS OFF)
    endif()

    add_custom_target(cleanup_depthai_spdlog
                      COMMAND rm -rf ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include/spdlog
    )

    add_dependencies(cleanup_depthai_spdlog DepthAI_ext)
endif()
