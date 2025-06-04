find_package(Open3D QUIET CONFIG)

if(NOT Open3D_FOUND)
    get_external_for_plugin(Filament)
    externalproject_add(Open3D_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/Open3D.git
                        GIT_TAG 53eacf9eb4a6e31f4dccbb361d268efe54f9dac2
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/Open3D
                        DEPENDS ${Filament_DEP_STR}
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DUSE_SYSTEM_BLAS=ON -DUSE_SYSTEM_TBB=ON -DUSE_SYSTEM_STDGPU=ON -DUSE_SYSTEM_CUTLASS=ON
    )
    set(Open3D_EXTERNAL Yes)
    set(Open3D_DEP_STR "Open3D_ext")
else()
    set(Open3D_VERSION ${Open3D_Version} PARENT_SCOPE)
endif()
