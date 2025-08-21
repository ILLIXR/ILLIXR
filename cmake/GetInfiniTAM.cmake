get_external_for_plugin(Draco)
if (NOT Infinitam_FOUND)
    externalproject_add(InfiniTAM_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/InfiniTAM.git
                        GIT_TAG dd732790f7ca3c3aff6fe4f8ab89b9da3eaef6c0
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/InfiniTAM
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} ${DEPENDENCY_COMPILE_ARGS}
                        DEPENDS ${Draco_DEP_STR}
    )
endif()
