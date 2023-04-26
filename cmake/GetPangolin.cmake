find_package(Pangolin 0.6 EXACT QUIET)

if(NOT Pangolin_FOUND)
    EXTERNALPROJECT_ADD(Pangolin
            GIT_REPOSITORY https://github.com/stevenlovegrove/Pangolin.git
            GIT_TAG dd801d244db3a8e27b7fe8020cd751404aa818fd
            PREFIX ${CMAKE_BINARY_DIR}/_deps/Pangolin
            PATCH_COMMAND patch -p0 src/config.h.in < ${PROJECT_SOURCE_DIR}/cmake/pangolin.patch
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=Release
            )
    set(Pangolin_EXTERNAL Yes)
    set(Pangolin_DEP_STR "Pangolin")
endif()
