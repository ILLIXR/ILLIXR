find_package(survive QUIET)
list(APPEND EXTERNAL_PROJECTS survive)
if (NOT survive_FOUND)
    pkg_check_modules(survive QUIET survive)
endif()

if(NOT survive_FOUND)
    fetch_git(NAME survive
              REPO https://github.com/collabora/libsurvive.git
              TAG 4fb6d888d0277a8a3ba725e63707434d80ecdb2a
              RECURSE
    )

    configure_target(NAME survive)
    #set(cnkalman_LIBRARIES cnkalman)
    #set(cnkalman_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/cnkalman ${CMAKE_INSTALL_PREFIX}/include/cnkalman/redist)
    #set(cnmatrix_LIBRARIES cnmatrix)
    #set(cnmatrix_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include/cnmatrix ${CMAKE_INSTALL_PREFIX}/include/cnmatrix/redist)
    #set(survive_LIBRARIES "survive;${cnmatrix_LIBRARIES};${cnkalman_LIBRARIES}" CACHE STRING "" FORCE)
    #set(survive_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include/libsurvive;${CMAKE_INSTALL_PREFIX}/include/libsurvive/redist;${CMAKE_INSTALL_PREFIX}/include/cnmatrix/libsurvive; ${cnkalman_INCLUDE_DIRS};${cnmatrix_INCLUDE_DIRS}" CACHE STRING "" FORCE)
else()
    set(survive_VERSION ${survive_VERSION} PARENT_SCOPE)
endif()
set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
