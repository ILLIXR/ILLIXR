# CMake module to look for depthai
# if it is not found then it is downloaded and marked for compilation and install

find_package(depthai QUIET)
list(APPEND EXTERNAL_PROJECTS depthai)
if(depthai_FOUND)
    set(DepthAI_VERSION "${depthai_VERSION}" PARENT_SCOPE)   # set current version
else()
    EXTERNALPROJECT_ADD(DepthAI_ext
            GIT_REPOSITORY https://github.com/luxonis/depthai-core.git   # Git repo for source code
            GIT_TAG v2.29.0                                              # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/depthai                     # the build directory
            #arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release ${DEPTHAI_CMAKE_ARGS} -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE} -DCMAKE_POLICY_VERSION_MINIMUM=3.5
            )
    # set variables for use by modules that depend on this one
    set(DepthAI_DEP_STR DepthAI_ext)   # Dependency string for other modules that depend on this one
    set(DepthAI_EXTERNAL Yes)      # Mark that this module is being built
    set(DepthAI_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include ${CMAKE_INSTALL_PREFIX}/include/depthai-shared ${CMAKE_INSTALL_PREFIX}/include/depthai-shared/3rdparty ${CMAKE_INSTALL_PREFIX}/include/depthai ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include)
    set(DepthAI_LIBRARIES depthai-core;depthai-opencv)

    add_custom_target(cleanup_depthai_spdlog
                      COMMAND rm -rf ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include/spdlog
    )

    add_dependencies(cleanup_depthai_spdlog DepthAI_ext)
endif()
