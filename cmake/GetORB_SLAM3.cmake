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

if(TARGET DBoW2_OS3)
    add_dependencies(plugin.orb_slam3${ILLIXR_BUILD_SUFFIX} DBoW2_OS3)
endif()
if(TARGET g2o_core)
    add_dependencies(plugin.orb_slam3${ILLIXR_BUILD_SUFFIX} g2o_core g2o_csparse_extension g2o_ext_freeglut_minimal g2o_hierarchical g2o_incremental g2o_interactive g2o_interface g2o_opengl_helper g2o_parser g2o_simulator g2o_solver_cholmod g2o_solver_csparse g2o_solver_dense g2o_solver_eigen g2o_solver_pcg g2o_solver_slam2d_linear g2o_solver_structure_only g2o_stuff g2o_types_data g2o_types_icp g2o_types_sba g2o_types_sclam2d g2o_types_sim3 g2o_types_slam2d_addons g2o_types_slam2d g2o_types_slam3d_addons g2o_types_slam3d
    )
endif()
if(TARGET sophus)
    add_dependencies(plugin.orb_slam3${ILLIXR_BUILD_SUFFIX} sophus)
endif()
