fetch_git(NAME OpenVINS
          REPO https://github.com/ILLIXR/open_vins.git
          TAG c7fe7c09c3a16e4d7e1062fdce949aa145fb010a
)

set(TEMP_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS -L${CMAKE_INSTALL_PREFIX}/lib)
set(ILLIXR_INTEGRATION ON)
set(CMAKE_CXX_FLAGS ${TEMP_FLAGS})
set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)
configure_target(NAME OpenVINS
                 NO_FIND
                 MATCH_BUILD_TYPE
)
unset(ILLIXR_INTEGRATION)
