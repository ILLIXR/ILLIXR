# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0>=19)
pkg_check_modules(SPATIALAUDIO REQUIRED spatialaudio)

ExternalProject_Add(Audio_Pipeline
        GIT_REPOSITORY https://github.com/ILLIXR/audio_pipeline.git   # Git repo for source code
        GIT_TAG 5aaf2e73b0cb3bca76dee3c1fff83686b7c37910              # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${CMAKE_BINARY_DIR}/_deps/audio_pipeline               # the build directory
        DEPENDS ${SpatialAudio_DEP_STR} ${OpenCV_DEP_STR}  # dependencies of this module
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib\ -L${CMAKE_INSTALL_PREFIX}/lib64 -DILLIXR_ROOT=${PROJECT_SOURCE_DIR}/include -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX} -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${AUDIO_PIPELINE_CMAKE_ARGS}
        )
