# install dependencies of hand tracking
if (HT_ENABLE_GPU)
    set(POSTFIX "_gpu")
else()
    set(POSTFIX "")
endif()

externalproject_add(
        hand_tracking_dependencies${POSTFIX}
        GIT_REPOSITORY https://github.com/ILLIXR/hand_tracking_dependencies.git
        GIT_TAG 818f9fc431108ec4017c30f84a116199d8963813
        PREFIX ${CMAKE_BINARY_DIR}/_deps/hand_tracking_deps
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DENABLE_GPU=${HT_ENABLE_GPU} -DLIBRARY_POSTFIX=ht
        INSTALL_COMMAND ""
)

set(Hand_Tracking_Deps${POSTFIX}_EXTERNAL Yes PARENT_SCOPE)
list(APPEND EXTERNAL_LIBRARIES "Hand_Tracking_Deps${POSTFIX}")
set(EXTERNAL_LIBRARIES ${EXTERNAL_LIBRARIES} PARENT_SCOPE)

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
        GIT_TAG c5d8138e376595d305f9b52beac162d2ff883296
        PREFIX ${PRFX}
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DHT_ENABLE_GPU=${HT_ENABLE_GPU} -DTFLIBRARY_POSTFIX=ht -DILLIXR_ROOT=${CMAKE_SOURCE_DIR} -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} -DBUILD_OXR_INTERFACE=${BUILD_OXR_INTERFACE}
        DEPENDS hand_tracking_dependencies${POSTFIX}
)
