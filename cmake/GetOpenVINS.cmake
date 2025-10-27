fetch_git(NAME OpenVINS
        REPO https://github.com/ILLIXR/open_vins.git
                     GIT_TAG 4976aa6426e01dad6115d09ad575d84a9ae575da         # sha5 hash for specific commit to pull (if there is no specific tag to use)
)

set(TEMP_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS -L${CMAKE_INSTALL_PREFIX}/lib)
set(ILLIXR_INTEGRATION ON)
set(CMAKE_CXX_FLAGS ${TEMP_FLAGS})
configure_target(NAME OpenVINS
                 NO_FIND
                 MATCH_BUILD_TYPE
)
unset(ILLIXR_INTEGRATION)
