find_package(KimeraRPGO 1.0 QUIET)

if(NOT KimeraRPGO_FOUND)
    if(USE_V4_CODE)
        ExternalProject_Add(KimeraRPGO
                GIT_REPOSITORY https://github.com/MIT-SPARK/Kimera-RPGO.git
                GIT_TAG 3718a6f29dc1de5c2966fcbd679eee27a7933a68
                DEPENDS ${GTSAM_DEP_STR}
                PREFIX ${CMAKE_BINARY_DIR}/_deps/kimerarpgo
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                )
    else()
        ExternalProject_Add(KimeraRPGO
                URL https://github.com/MIT-SPARK/Kimera-RPGO/archive/refs/tags/dec-2020.tar.gz
                https://github.com/MIT-SPARK/Kimera-RPGO.git
                3718a6f29dc1de5c2966fcbd679eee27a7933a68
                URL_HASH MD5=26ac6e1525523b88d555e61ca74da34b
                DEPENDS ${GTSAM_DEP_STR}
                PREFIX ${CMAKE_BINARY_DIR}/_deps/kimerarpgo
                CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                )
    endif()
    set(KimeraRPGO_EXTERNAL Yes)
    set(KimeraRPGO_DEP_STR "KimeraRPGO")
endif()