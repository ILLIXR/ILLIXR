pkg_check_modules(draco QUIET draco)

if (NOT draco_found)
    externalproject_add(Draco_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/draco.git
                        GIT_TAG bba2a71ae3d46631a3b6d969e60730d570e904aa
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/draco
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DDRACO_TRANSCODER_SUPPORTED=ON -DCMAKE_BUILD_TYPE=Release ${DEPENDENCY_COMPILE_ARGS}
    )
    set(Draco_DEP_STR Draco_ext)
    set(Draco_EXTERNAL Yes)
endif()
