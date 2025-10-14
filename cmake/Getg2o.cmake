# CMake module to look for g2o
# if it is not found then it is downloaded and marked for compilation and install

list(APPEND EXTERNAL_PROJECTS g2o)
find_package(g2o 1.0 QUIET)
if (NOT g2o_FOUND)
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "g2o should be installed with vcpkg")
    endif()
    fetch_git(NAME g2o
              REPO https://github.com/RainerKuemmerle/g2o.git
              TAG 20241228_git
    )

    set(FLAGS_TEMP ${CMAKE_CXX_FLAGS})
    set(CMAKE_CXX_FLAGS "-L${CMAKE_INSTALL_PREFIX}/lib")
    configure_target(NAME g2o
                     VERSION 1.0
    )
    set(CMAKE_CXX_FLAGS ${FLAGS_TEMP})
    unset(FLAGS_TEMP)
else()
    set(g2o_VERSION ${g2o_VERSION} PARENT_SCOPE)
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
