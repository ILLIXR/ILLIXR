# CMake module to look for g2o
# if it is not found then it is downloaded and marked for compilation and install

find_package(g2o 1.0 QUIET)
if (g2o_FOUND)
    set(g2o_VERSION ${g2o_VERSION} PARENT_SCOPE)
    message(STATUS "  Found g2o, ${g2o_VERSION}")
else()
    fetch_git(NAME g2o
              REPO https://github.com/RainerKuemmerle/g2o.git
              TAG 20241228_git
              PATCH
              NO_OVERRIDE
    )
    configure_target(NAME g2o
                     NO_FIND
    )

    message("SETTING g2o_DIR to ${g2o_BINARY_DIR}/generated")
    set(g2o_DIR ${g2o_BINARY_DIR}/generated CACHE PATH "" FORCE)
    message("    set to ${g2o_DIR}")
endif()
