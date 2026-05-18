# CMake module to look for DBoW2
# if it is not found then it is downloaded and marked for compilation and install

find_package(DBoW2_OS3 QUIET)

if(DBoW2_OS3_LIBRARIES)
    set(DBoW2_VERSION "OS3" PARENT_SCOPE)
    message(STATUS "  Found DBoW2, ${DBoW2_VERSION}")
else()
    fetch_git(NAME DBoW2_OS3
              REPO https://github.com/ILLIXR/DBoW2_OS3.git      # patch it in source
              TAG 352e07bb757bbb8b426ad48331da2cab68bca657
              NO_OVERRIDE
    )
    configure_target(NAME DBoW2_OS3
                     NO_FIND
    )

    set(DBoW2_OS3_DIR ${DBoW2_OS3_BINARY_DIR} CACHE PATH "" FORCE)
endif()
