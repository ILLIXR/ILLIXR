if (NOT Infinitam_FOUND)
    externalproject_add(InfiniTAM_ext
                        GIT_REPOSITORY https://github.com/ILLIXR/InfiniTAM.git
                        GIT_TAG cc6f2c67026b551fa2be8f347c1aaf8ed50fcefb
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/InfiniTAM
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DILLIXR_ROOT=${CMAKE_SOURCE_DIR}/include -DILLIXR_BUILD_SUFFIX=${ILLIXR_BUILD_SUFFIX} -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE} -DOpenCV_DIR=${OpenCV_DIR} -DBoost_DIR=${Boost_DIR} -DOpenGL_DIR=${OpenGL_DIR}
                        DEPENDS ${Draco_DEP_STR}
    )
endif()
