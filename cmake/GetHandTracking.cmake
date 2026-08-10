# install dependencies of hand tracking
if (HT_ENABLE_GPU)
    set(POSTFIX "_gpu")
else()
    set(POSTFIX "")
endif()

fetch_git(NAME hand_tracking_dependencies${POSTFIX}
          REPO https://github.com/ILLIXR/hand_tracking_dependencies.git
          TAG 4fa06f9fff7610a0c686c06468b95da5bcf8aca5
)

set(ENABLE_GPU ${HT_ENABLE_GPU})
set(LIBRARY_POSTFIX ht)
configure_target(NAME hand_tracking_dependencies${POSTFIX}
                 NO_FIND
)
unset(ENABLE_GPU)
unset(LIBRARY_POSTFIX)

# hand tracking plugin
set(PRFX ${CMAKE_BINARY_DIR}/_deps/hand_tracking)
set(HT_TARGET_NAME "HAND_TRACKING")
#if(HT_ENABLE_GPU)
#    set(PRFX "${PRFX}_gpu")
#    set(HT_TARGET_NAME "${HT_TARGET_NAME}_GPU")
#endif()

fetch_git(NAME ${HT_TARGET_NAME}
          REPO https://github.com/ILLIXR/hand_tracking.git
          TAG 18ca57cbf0d8ff9dc34838e84612cfa1e1cfa9fb
          DEPENDS hand_tracking_dependencies${POSTFIX}
)

set(HT_ENABLE_GPU ${HT_ENABLE_GPU})
set(TFLIBRARY_POSTFIX ht)
set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR})

configure_target(NAME ${HT_TARGET_NAME}
                 MATCH_BUILD_TYPE
                 NO_FIND
)

unset(HT_ENABLE_GPU)
unset(TFLIBRARY_POSTFIX)
unset(ILLIXR_ROOT)
