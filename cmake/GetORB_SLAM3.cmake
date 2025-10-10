# module to download, build and install the ORM_SLAM3 ILLIXR plugin

# get dependencies
get_external_for_plugin(g2o)
get_external_for_plugin(Sophus)
get_external_for_plugin(DBoW2)

set(ORB_SLAM3_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/ORB_Slam3")

fetch_git(NAME ORB_Slam3
          REPOSITORY https://github.com/ILLIXR/ORB_SLAM3.git
          TAG 0b69d260ea3cc0b4c723684ff0d2a5a92efa8a2e
)

set(TEMP_FLAGS ${CMAKE_CXX_FLAGS})
set(CMAKE_CXX_FLAGS -L${CMAKE_INSTALL_PREFIX}/lib)
configure_target(NAME ORB_Slam3
                 NO_FIND
                 MATCH_BUILD_TYPE
)
set(CMAKE_CXX_FLAGS ${TEMP_FLAGS})
unset(TEMP_FLAGS)

DEPENDS ${DBoW2_DEP_STR} ${g2o_DEP_STR} ${Sophus_DEP_STR}  # dependencies of this module
