# Modifying a plugin

## Tutorial

This is how you can modify an existing ILLIXR plugin. This example uses the Audio Pipeline plugin, but the steps can be applied to any plugin.

1.  Fork the repository for the component you want to modify into your own repo using the github
    web interface, then pull your repo to your computer. For example, using the Audio Pipeline plugin:
    ```bash
    git clone https://github.com/<YOUR_USER_NAME>/audio_pipeline.git
    ```

        

1.  Modify the associated `cmake/GetAudioPipeline.cmake`
    original
    ```cmake
    get_external(PortAudio)
    get_external(SpatialAudio)

    set(AUDIO_PIPELINE_CMAKE_ARGS "")
    if(HAVE_CENTOS)
        set(AUDIO_PIPELINE_CMAKE_ARGS "-DINTERNAL_OPENCV=${OpenCV_DIR}")
    endif()
    ExternalProject_Add(Audio_Pipeline
        GIT_REPOSITORY https://github.com/ILLIXR/audio_pipeline.git
        GIT_TAG 714c3541378ece7b481804e4a504e23b49c2bdbe
        PREFIX ${CMAKE_BINARY_DIR}/_deps/audio_pipeline
        DEPENDS ${PortAudio_DEP_STR} ${SpatialAudio_DEP_STR} ${OpenCV_DEP_STR}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib\ -L${CMAKE_INSTALL_PREFIX}/lib64 -DILLIXR_ROOT=${PROJECT_SOURCE_DIR}/include -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_INSTALL_LIBDIR=lib -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${AUDIO_PIPELINE_CMAKE_ARGS}
     )
    ```
    which becomes `cmake/GetMyAudioPipeline.cmake`
    ```cmake
    get_external(PortAudio)
    get_external(SpatialAudio)

    set(AUDIO_PIPELINE_CMAKE_ARGS "")
    if(HAVE_CENTOS)
        set(AUDIO_PIPELINE_CMAKE_ARGS "-DINTERNAL_OPENCV=${OpenCV_DIR}")
    endif()
    ExternalProject_Add(Audio_Pipeline
        GIT_REPOSITORY https://github.com/<YOUR_USER_NAME>/audio_pipeline.git
        PREFIX ${CMAKE_BINARY_DIR}/_deps/myaudio_pipeline
        DEPENDS ${PortAudio_DEP_STR} ${SpatialAudio_DEP_STR} ${OpenCV_DEP_STR}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib\ -L${CMAKE_INSTALL_PREFIX}/lib64 -DILLIXR_ROOT=${PROJECT_SOURCE_DIR}/include -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX} -DCMAKE_INSTALL_LIBDIR=lib -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${AUDIO_PIPELINE_CMAKE_ARGS}
     )
    ```
   
1.  Make whatever changes to the plugin code you want and be sure to push them to your forked repo.

1.  See the instructions on [Getting Started][10] to learn how to build and run ILLIXR.

1.  To push the modification to upstream ILLIXR, create a PR to the original repository.


[//]: # (- Internal -)

[10]:   getting_started.md
