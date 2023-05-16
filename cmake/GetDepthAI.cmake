find_package(depthai QUIET)

set(DEPTHAI_CMAKE_ARGS "")
if(HAVE_CENTOS)
    set(DEPTHAI_CMAKE_ARGS "-DOpenCV_DIR=${OpenCV_DIR}")
endif()

if(depthai_LIBRARIES)
    set(depthai_VERSION "2.20.2")
else()
    EXTERNALPROJECT_ADD(DepthAI_ext
            GIT_REPOSITORY https://github.com/luxonis/depthai-core.git
            GIT_TAG 4ff860838726a5e8ac0cbe59128c58a8f6143c6c
            PREFIX ${CMAKE_BINARY_DIR}/_deps/depthai
            DEPENDS ${OpenCV_DEP_STR}
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release ${DEPTHAI_CMAKE_ARGS} -DBUILD_SHARED_LIBS=ON
            )
    set(DepthAI_EXTERNAL Yes)
    set(DepthAI_DEP_STR depthai)
    set(DepthAI_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(DepthAI_LIBRARIES depthai-core;depthai-opencv;depthai-resources)
endif()
