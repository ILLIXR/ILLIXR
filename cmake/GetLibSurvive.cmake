find_package(survive QUIET)

if (NOT survive_FOUND)
    if(WIN32)
        find_library(LIBSURVIVE_LIBRARY
                     NAMES libsurvive
                     PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib"
                     "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/lib"
        )
        find_path(LIBSURVIVE_INCLUDE_DIR
                  NAMES survive.h
                  PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/libsurvive"
        )
        set(survive_INCLUDE_DIRS "${LIBSURVIVE_INCLUDE_DIR}" CACHE PATH "" FORCE)
        set(survive_LIBRARIES "${LIBSURVIVE_LIBRARY}" CACHE PATH "" FORCE)
        if(LIBSURVIVE_LIBRARY AND LIBSURVIVE_INCLUDE_DIR)
            set(survive_FOUND "1" CACHE INTERNAL "" FORCE)
            return()
        endif()
    else()
        pkg_check_modules(survive QUIET survive)
    endif()
endif()

if(survive_FOUND)
    message(STATUS "  Found survive, ${survive_VERSION}")
else()
    message("XYT")
    fetch_git(NAME LibSurvive_ext
              REPO https://github.com/collabora/libsurvive.git
              TAG 4fb6d888d0277a8a3ba725e63707434d80ecdb2a
              OVERRIDE_UPDATE
    )

    set(cnkalman_LIBRARIES cnkalman)
    set(cnkalman_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/cnkalman ${CMAKE_INSTALL_PREFIX}/include/cnkalman/redist)
    set(cnmatrix_LIBRARIES cnmatrix)
    set(cnmatrix_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/cnmatrix ${CMAKE_INSTALL_PREFIX}/include/cnmatrix/redist)
    set(survive_LIBRARIES "survive;${cnmatrix_LIBRARIES};${cnkalman_LIBRARIES}" CACHE STRING "" FORCE)
    set(survive_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/libsurvive;${CMAKE_INSTALL_PREFIX}/include/libsurvive/redist;${CMAKE_INSTALL_PREFIX}/include/cnmatrix/libsurvive; ${cnkalman_INCLUDE_DIRS};${cnmatrix_INCLUDE_DIRS}" CACHE STRING "" FORCE)

    configure_target(NAME LibSurvive_ext)

endif()
