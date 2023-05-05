find_package(g2o 1.0 QUIET)
if (NOT g2o_FOUND)
    EXTERNALPROJECT_ADD(g2o
            GIT_REPOSITORY https://github.com/RainerKuemmerle/g2o.git
            GIT_TAG 20230223_git
            PREFIX ${CMAKE_BINARY_DIR}/_deps/g2o
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release
            )
    set(g2o_DEP_STR "g2o")
    set(g2o_EXTERNAL YES)
endif()
