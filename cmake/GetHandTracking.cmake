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
set(PRFX ${CMAKE_BINARY_DIR}/_deps/hand_tracking)
set(HT_TARGET_NAME "HAND_TRACKING")
if(HT_ENABLE_GPU)
    set(PRFX "${PRFX}_gpu")
    set(HT_TARGET_NAME "${HT_TARGET_NAME}_GPU")
endif()
message("${HT_TARGET_NAME}")
externalproject_add(
        ${HT_TARGET_NAME}
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking.git
        GIT_TAG 360dae921cb9cae6796db6a59057ae54b0acd98e
        PREFIX ${PRFX}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DHT_ENABLE_GPU=${HT_ENABLE_GPU} -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX}
        DEPENDS hand_tracking_dependencies
)
