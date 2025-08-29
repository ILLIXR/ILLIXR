# module to download, build and install the ORM_SLAM3 ILLIXR plugin

# get dependencies
get_external_for_plugin(g2o)
get_external_for_plugin(Sophus)
get_external_for_plugin(DBoW2)

set(ORB_SLAM3_CMAKE_ARGS "")
set(ORB_SLAM3_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/ORB_Slam3")

EXTERNALPROJECT_ADD(ORB_Slam3
        GIT_REPOSITORY https://github.com/ILLIXR/ORB_SLAM3.git   # Git repo for source code
        GIT_TAG 0b69d260ea3cc0b4c723684ff0d2a5a92efa8a2e         # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${ORB_SLAM3_SOURCE_DIR}                            # the build directory
        DEPENDS ${DBoW2_DEP_STR} ${g2o_DEP_STR} ${Sophus_DEP_STR}  # dependencies of this module
        # force serialized build, otherwise the machine might get slogged down
        BUILD_COMMAND cmake --build . -j1
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_ROOT=${PROJECT_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${ORB_SLAM3_CMAKE_ARGS} -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE}
        )
