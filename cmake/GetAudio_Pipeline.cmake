# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0>=19)
pkg_check_modules(SPATIALAUDIO REQUIRED spatialaudio)

ExternalProject_Add(Audio_Pipeline
                    GIT_REPOSITORY https://github.com/ILLIXR/audio_pipeline.git   # Git repo for source code
                    GIT_TAG f2603d835005250652634f7f25466e51d1b72892              # sha5 hash for specific commit to pull (if there is no specific tag to use)
                    PREFIX ${CMAKE_BINARY_DIR}/_deps/audio_pipeline               # the build directory
                    #arguments to pass to CMake
                    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_INTEGRATION=ON -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${DEPENDENCY_COMPILE_ARGS}
)
