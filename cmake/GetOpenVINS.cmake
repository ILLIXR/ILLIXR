fetch_git(NAME OpenVINS
          REPO https://github.com/ILLIXR/open_vins.git
          TAG 4a84fa5754a88a0898a0c4836b7e591c15c5117f
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
