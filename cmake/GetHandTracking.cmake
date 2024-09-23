# install dependencies of hand tracking
externalproject_add(
        hand_tracking_dependencies
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking_dependencies.git
        GIT_TAG d60b0d9ee6e98130642d0c4b88ee976ea557502e
        PREFIX ${CMAKE_BINARY_DIR}/_deps/hand_tracking_deps
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release
        INSTALL_COMMAND ""
)

# hand tracking plugin
externalproject_add(
        HAND_TRACKING
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking.git
        GIT_TAG 55dddb92b9b2a258c6bc42b23f15002d0d153aa2
        PREFIX ${CMAKE_BINARY_DIR}/_deps/hand_tracking
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DHT_ENABLE_GPU=OFF -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX}
        DEPENDS hand_tracking_dependencies
)
