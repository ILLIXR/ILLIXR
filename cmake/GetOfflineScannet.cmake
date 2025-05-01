ExternalProject_Add(Offline_Scannet
                    GIT_REPOSITORY https://github.com/YanaR05/Open3D_plugin.git
                    GIT_TAG dc70d7556c40345fa6ae00ab1c2732a490d0d420
                    PREFIX ${CMAKE_BINARY_DIR}/_deps/offline_scannet
                    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                    SOURCE_SUBDIR offline_scannet
)
