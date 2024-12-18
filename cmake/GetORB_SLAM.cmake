# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
get_external_for_plugin(Pangolin)
get_external_for_plugin(g2o)
get_external_for_plugin(Sophus)
get_external_for_plugin(DBoW2)

set(ORB_SLAM_CMAKE_ARGS "")
set(ORB_SLAM_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/ORB_Slam")
# if building on CentOS make sure we use the correct OpenCV
if(HAVE_CENTOS)
    set(ORB_SLAM_CMAKE_ARGS "-DINTERNAL_OPENCV=${OpenCV_DIR}")
endif()

EXTERNALPROJECT_ADD(ORB_Slam
        GIT_REPOSITORY https://github.com/ILLIXR/ORB_SLAM3.git   # Git repo for source code
        GIT_TAG 6a3afc5bef01a72fa64ff48429064c13af04c116         # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${ORB_SLAM_SOURCE_DIR}                            # the build directory
        DEPENDS ${DBoW2_DEP_STR} ${Pangolin_DEP_STR} ${g2o_DEP_STR} ${Sophus_DEP_STR} ${OpenCV_DEP_STR}   # dependencies of this module
        # force serialized build, otherwise the machine might get slogged down
        BUILD_COMMAND cmake --build . -j1
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_ROOT=${PROJECT_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${ORB_SLAM_CMAKE_ARGS}
        )
