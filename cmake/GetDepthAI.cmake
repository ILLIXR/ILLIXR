# CMake module to look for depthai
# if it is not found then it is downloaded and marked for compilation and install

find_package(depthai QUIET)

set(DEPTHAI_CMAKE_ARGS "")

# if building on CentOS make sure we use the correct OpenCV
if(HAVE_CENTOS)
    set(DEPTHAI_CMAKE_ARGS "-DOpenCV_DIR=${OpenCV_DIR}")
endif()

if(depthai_FOUND)
    set(DepthAI_VERSION "${depthai_VERSION}" PARENT_SCOPE)   # set current version
else()
    EXTERNALPROJECT_ADD(DepthAI_ext
            GIT_REPOSITORY https://github.com/luxonis/depthai-core.git   # Git repo for source code
            GIT_TAG 4ff860838726a5e8ac0cbe59128c58a8f6143c6c             # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/depthai                     # the build directory
            DEPENDS ${OpenCV_DEP_STR}                                    # dependencies of this module
            # the code needs to be patched to build on some systems
            PATCH_COMMAND ${PROJECT_SOURCE_DIR}/cmake/do_patch.sh -p ${PROJECT_SOURCE_DIR}/cmake/Depthai.patch
            #arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release ${DEPTHAI_CMAKE_ARGS} -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_LIBDIR=lib
            )
    # set variables for use by modules that depend on this one
    set(DepthAI_DEP_STR depthai)   # Dependency string for other modules that depend on this one
    set(DepthAI_EXTERNAL Yes)      # Mark that this module is being built
    set(DepthAI_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include ${CMAKE_INSTALL_PREFIX}/include/depthai-shared ${CMAKE_INSTALL_PREFIX}/include/depthai-shared/3rdparty ${CMAKE_INSTALL_PREFIX}/include/depthai ${CMAKE_INSTALL_PREFIX}/lib/cmake/depthai/dependencies/include)
    set(DepthAI_LIBRARIES depthai-core;depthai-opencv)
endif()
