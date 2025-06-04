find_package(filament QUIET CONFIG)

if(NOT filament_FOUND)
    externalproject_add(Filament_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/filament.git
                        GIT_TAG 954facbd2f37cdd7298723c41e29092e7da68492
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/filament
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DUSE_STATIC_LIBCXX=OFF     -DFILAMENT_SKIP_SAMPLES=ON -DFILAMENT_USE_EXTERNAL_GLES3=ON -DFILAMENT_SUPPORTS_WAYLAND=ON -DFILAMENT_SUPPORTS_VULKAN=ON -DFILAMENT_BUILD_FILAMAT=ON
    )
    set(Filament_DEP_STR "Filament_ext")
    set(Filament_EXTERNAL Yes)
else()
    set(Filament_VERSION ${filament_VERSION} PARENT_SCOPE)
endif()
