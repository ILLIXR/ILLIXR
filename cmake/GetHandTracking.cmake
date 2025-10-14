# install dependencies of hand tracking
if (HT_ENABLE_GPU)
    set(POSTFIX "_gpu")
else()
    set(POSTFIX "")
endif()
if(UNIX) # on windows this is done via vcpkg
    fetch_git(NAME hand_tracking_dependencies${POSTFIX}
              REPO https://github.com/ILLIXR/hand_tracking_dependencies.git
              TAG 8dc45876a9eebc3ad6900fafa5b6ca144e290a9f
    )

    set(ENABLE_GPU ${HT_ENABLE_GPU})
    set(LIBRARY_POSTFIX ht)
    configure_target(hand_tracking_dependencies${POSTFIX})
    unset(ENABLE_GPU)
endif()

# hand tracking plugin
set(HT_TARGET_NAME "HAND_TRACKING")
if(HT_ENABLE_GPU)
    set(HT_TARGET_NAME "${HT_TARGET_NAME}_GPU")
endif()

fetch_git(NAME ${HT_TARGET_NAME}
          REPOS https://github.com/ILLIXR/hand_tracking.git
          TAG 0cc6e2cb04514001f6ef8c3f3bea348cc1d965ea
)

set(TFLIBRARY_POSTFIX ${LIBRARY_POSTFIX})
set(BUILD_OXR_INTERFACE ${BUILD_OXR_INTERFACE})
set(BUILD_OXR_TEST ${BUILD_OXR_TEST})
configure_target(NAME ${HT_TARGET_NAME}
                 MATCH_BUILD_TYPE
                 NO_FIND
)
unset(TFLIBRARY_POSTFIX)
unset(BUILD_OXR_INTERFACE)
unset(BUILD_OXR_TEST)

unset(LIBRARY_POSTFIX)
