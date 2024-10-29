# install dependencies of hand tracking
externalproject_add(
        hand_tracking_dependencies
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking_dependencies.git
        GIT_TAG fe7eb0e736b80b19335b4faaaea09c70fc198d25
        PREFIX ${CMAKE_BINARY_DIR}/_deps/hand_tracking_deps
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DENABLE_GPU=${HT_ENABLE_GPU} -DLIBRARY_POSTFIX=ht
        INSTALL_COMMAND ""
)

# hand tracking plugin
set(PRFX ${CMAKE_BINARY_DIR}/_deps/hand_tracking)
set(HT_TARGET_NAME "HAND_TRACKING")
if(HT_ENABLE_GPU)
    set(PRFX "${PRFX}_gpu")
    set(HT_TARGET_NAME "${HT_TARGET_NAME}_GPU")
endif()
externalproject_add(
        ${HT_TARGET_NAME}
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking.git
        GIT_TAG 6917d4e0261e438a87ff954c9c9b3302291b788e
        PREFIX ${PRFX}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DHT_ENABLE_GPU=${HT_ENABLE_GPU} -DTFLIBRARY_POSTFIX=ht -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX}
        DEPENDS hand_tracking_dependencies
)
