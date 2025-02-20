# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
get_external(PortAudio)
get_external(SpatialAudio)

ExternalProject_Add(Audio_Pipeline
                    GIT_REPOSITORY https://github.com/ILLIXR/audio_pipeline.git   # Git repo for source code
                    GIT_TAG b8a91f41866d9ed65f065cf2f45c78ddc929167f              # sha5 hash for specific commit to pull (if there is no specific tag to use)
                    PREFIX ${CMAKE_BINARY_DIR}/_deps/audio_pipeline               # the build directory
                    DEPENDS ${PortAudio_DEP_STR} ${SpatialAudio_DEP_STR} ${OpenCV_DEP_STR}  # dependencies of this module
                    #arguments to pass to CMake
                    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX}
)
