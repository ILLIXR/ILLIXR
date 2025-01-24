# install dependencies of hand tracking
externalproject_add(
        hand_tracking_dependencies
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking_dependencies.git
        GIT_TAG 818f9fc431108ec4017c30f84a116199d8963813
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
        GIT_TAG ee5006bb53c0ba3ac59f7a3f804b1e3bf823a080
        PREFIX ${PRFX}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Debug -DHT_ENABLE_GPU=${HT_ENABLE_GPU} -DTFLIBRARY_POSTFIX=ht -DILLIXR_ROOT=${CMAKE_SOURCE_DIR} -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} -DBUILD_OXR_INTERFACE=${BUILD_OXR_INTERFACE}
        DEPENDS hand_tracking_dependencies
)
