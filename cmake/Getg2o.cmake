# CMake module to look for g2o
# if it is not found then it is downloaded and marked for compilation and install

list(APPEND EXTERNAL_PROJECTS g2o)
find_package(g2o 1.0 QUIET)
if (NOT g2o_FOUND)
    EXTERNALPROJECT_ADD(g2o
                        GIT_REPOSITORY https://github.com/RainerKuemmerle/g2o.git   # Git repo for source code
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/g2o                        # the build directory
                        # arguments to pass to CMake
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_CXX_FLAGS="-L${CMAKE_INSTALL_PREFIX}/lib" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=${CLANG_CXX_EXE} -DCMAKE_C_COMPILER=${CLANG_EXE}
    )
    # set variables for use by modules that depend on this one
    set(g2o_DEP_STR "g2o")     # Dependency string for other modules that depend on this one
    set(g2o_EXTERNAL YES)      # Mark that this module is being built
else()
    set(g2o_VERSION ${g2o_VERSION} PARENT_SCOPE)
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
