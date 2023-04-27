find_package(Sophus 1.22 QUIET)
if (NOT Sophus_FOUND)
    find_package(fmt REQUIRED)
    EXTERNALPROJECT_ADD(Sophus
            GIT_REPOSITORY https://github.com/strasdat/Sophus.git
            GIT_TAG 1.22.10
            PREFIX ${CMAKE_BINARY_DIR}/_deps/Sophus
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release -DBUILD_SOPHUS_TESTS=OFF -DBUILD_SOPHUS_EXAMPLES=OFF
            )
    set(Sophus_DEP_STR "Sophus")
    set(Sophus_EXTERNAL Yes)
endif()
