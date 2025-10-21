find_package(survive QUIET)
list(APPEND EXTERNAL_PROJECTS survive)
if (NOT survive_FOUND)
    pkg_check_modules(survive QUIET survive)
endif()

if(NOT survive_FOUND)
    EXTERNALPROJECT_ADD(LibSurvive_ext
                        GIT_REPOSITORY https://github.com/collabora/libsurvive.git
                        GIT_TAG 4fb6d888d0277a8a3ba725e63707434d80ecdb2a
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/survive
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE}
                        UPDATE_COMMAND git submodule update --init
    )
    set(LibSurvive_DEP_STR LibSurvive_ext)
    set(LibSurvive_EXTERNAL Yes)
    set(cnkalman_LIBRARIES cnkalman)
    set(cnkalman_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/cnkalman ${CMAKE_INSTALL_PREFIX}/include/cnkalman/redist)
    set(cnmatrix_LIBRARIES cnmatrix)
    set(cnmatrix_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/cnmatrix ${CMAKE_INSTALL_PREFIX}/include/cnmatrix/redist)
    set(survive_LIBRARIES "survive;${cnmatrix_LIBRARIES};${cnkalman_LIBRARIES}" CACHE STRING "" FORCE)
    set(survive_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/libsurvive;${CMAKE_INSTALL_PREFIX}/include/libsurvive/redist;${CMAKE_INSTALL_PREFIX}/include/cnmatrix/libsurvive; ${cnkalman_INCLUDE_DIRS};${cnmatrix_INCLUDE_DIRS}" CACHE STRING "" FORCE)
endif()
set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
