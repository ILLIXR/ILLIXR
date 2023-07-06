# module to download, build and install the Kimera_VIO ILLIXR plugin

# get dependencies
get_external(opengv)
get_external(KimeraRPGO)

set(KIMERA_VIO_CMAKE_ARGS "")

# if building on CentOS make sure we use the correct OpenCV
if(HAVE_CENTOS)
    set(KIMERA_VIO_CMAKE_ARGS "-DINTERNAL_OPENCV=${OpenCV_DIR}")
endif()
ExternalProject_Add(Kimera_VIO
        GIT_REPOSITORY https://github.com/ILLIXR/Kimera-VIO.git   # Git repo for source code
        GIT_TAG 129803e0434b12d9fee9c9af4599839b5a19789f          # sha5 hash for specific commit to pull (if there is no specific tag to use)
        DEPENDS ${KimeraRPGO_DEP_STR} ${DBoW2_DEP_STR} ${GTSAM_DEP_STR} ${opengv_DEP_STR} ${OpenCV_DEP_STR}  # dependencies of this module
        PREFIX ${CMAKE_BINARY_DIR}/_deps/kimera_vio               # the build directory
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_SHARED_LINKER_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib -DCMAKE_EXE_LINKER_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib -DBUILD_TESTS=OFF -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} -DCMAKE_INSTALL_LIBDIR=lib -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include ${KIMERA_VIO_CMAKE_ARGS}
        )
