
fetch_git(NAME OpenVINS
        REPO https://github.com/ILLIXR/open_vins.git
        TAG 06146ec3be7167a2f4b519e104912ea814738e9a
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
