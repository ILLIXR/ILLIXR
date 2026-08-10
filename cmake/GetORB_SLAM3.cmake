# module to download, build and install the ORM_SLAM3 ILLIXR plugin

# get dependencies
get_external_for_plugin(g2o)
find_package(g2o REQUIRED)
get_external_for_plugin(Sophus)
find_package(Sophus REQUIRED)
get_external_for_plugin(DBoW2)
find_package(DBoW2_OS3)

fetch_git(NAME ORB_Slam3
          REPO https://github.com/ILLIXR/ORB_SLAM3.git
          TAG 88496310e4794cabef4c21ca75a8c1fb56a7e7e6
          OVERRIDE_BUILD "cmake --build . -j1"
)


#-DCMAKE_CXX_FLAGS=-L${CMAKE_INSTALL_PREFIX}/lib
set(ILLIXR_ROOT ${PROJECT_SOURCE_DIR}/include)
configure_target(NAME ORB_Slam3
                 MATCH_BUILD_TYPE
                 NO_FIND
)
unset(ILLIXR_ROOT)
