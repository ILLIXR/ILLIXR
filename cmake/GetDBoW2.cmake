find_package(DBoW2 QUIET)

if(DBoW2_LIBRARIES)
    set(DBoW2_VERSION "0.0")
else()
    EXTERNALPROJECT_ADD(DBoW2
            GIT_REPOSITORY https://github.com/dorian3d/DBoW2.git
            GIT_TAG 3924753db6145f12618e7de09b7e6b258db93c6e
            PREFIX ${CMAKE_BINARY_DIR}/_deps/DBoW2
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            )
    set(DBoW2_DEP_STR "DBoW2")
    set(DBoW2_EXTERNAL Yes)
endif()
