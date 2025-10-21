
fetch_git(NAME OpenVINS
        REPO https://github.com/ILLIXR/open_vins.git
        TAG 489ee07f27e6ccdf8db0e734e54c2b9fc0061db9
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
