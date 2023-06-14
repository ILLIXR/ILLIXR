find_package(opengv 1.0 QUIET)

if(NOT opengv_FOUND)
    EXTERNALPROJECT_ADD(opengv
            GIT_REPOSITORY https://github.com/laurentkneip/opengv.git
            GIT_TAG 91f4b19c73450833a40e463ad3648aae80b3a7f3
            PREFIX ${CMAKE_BINARY_DIR}/_deps/opengv
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
            )
    set(opengv_DEP_STR "opengv")
    set(opengv_EXTERNAL Yes)
endif()