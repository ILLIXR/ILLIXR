pkg_check_modules(draco QUIET draco)

if (NOT draco_found)
    externalproject_add(Draco_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/draco.git
                        GIT_TAG 97656cd3d94875184894fb2ca3eb1f79ecde0211
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/draco
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DDRACO_TRANSCODER_SUPPORTED=ON -DCMAKE_CXX_COMPILER=${CLANGPP_EXEC} -DCMAKE_C_COMPILER=${CLANG_EXEC}
    )
    set(Draco_DEP_STR Draco_ext)
    set(Draco_EXTERNAL Yes)
endif()
