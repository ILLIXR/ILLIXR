get_external_for_plugin(Open3D)

ExternalProject_Add(Offline_Scannet
                    GIT_REPOSITORY https://github.com/astro-friedel/Open3D_plugin.git
                    GIT_TAG dd6dcc4938f7ff58f7d49d29a396b94432202f86
                    PREFIX ${CMAKE_BINARY_DIR}/_deps/offline_scannet
                    DEPENDS ${Open3D_DEP_STR}
                    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DILLIXR_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX}
                    SOURCE_SUBDIR offline_scannet
)
