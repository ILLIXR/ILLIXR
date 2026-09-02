# CMake module to look for GTSAM
# if it is not found then it is downloaded and marked for compilation and install

find_package(GTSAM 4.3.0 QUIET EXACT)

if(GTSAM_FOUND)
    message(STATUS "  Found GTSAM, 4.3.0")
    if(WIN32 OR MSVC)
        if (NOT GTSAM_UNSTABLE_DIR)
            set(GTSAM_UNSTABLE_DIR "${GTSAM_DIR}" CACHE PATH "" FORCE)
        endif()
        find_package(GTSAM_UNSTABLE 4.3.0 QUIET EXACT)
    else()
        find_package(GTSAM_UNSTABLE 4.3.0 QUIET EXACT)
    endif()

    if(GTSAM_UNSTABLE_FOUND)
        message(STATUS "  Found GTSAM_UNSTABLE, 4.3.0")
        return()
    else()
        message(STATUS "  GTSAM_UNSTABLE not found, will build GTSAM from source")
    endif()
endif()

if(BUILD_GTSAM)
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "GTSAM should be installed with vcpkg")
    endif()
    fetch_git(NAME GTSAM
              REPO https://github.com/ILLIXR/gtsam.git
              TAG 135f09fe08f749596a03d4d018387f4590f826c1
    )
    set(GTSAM_WITH_TBB OFF)
    set(GTSAM_USE_SYSTEM_EIGEN ON)
    set(GTSAM_POSE3_EXPMAP ON)
    set(GTSAM_ROT3_EXPMAP ON)
    set(GTSAM_BUILD_TESTS OFF)
    set(GTSAM_BUILD_EXAMPLES_ALWAYS OFF)
    configure_target(NAME GTSAM
                     VERSION 4.3a0
    )
    unset(GTSAM_WITH_TBB)
    unset(GTSAM_USE_SYSTEM_EIGEN)
    unset(GTSAM_POSE3_EXPMAP)
    unset(GTSAM_ROT3_EXPMAP)
    unset(GTSAM_BUILD_TESTS)
    unset(GTSAM_BUILD_EXAMPLES_ALWAYS)
else()
    set(GTSAM_VERSION ${GTSAM_VERSION} PARENT_SCOPE)
endif()

fetch_git(NAME GTSAM
          REPO https://github.com/ILLIXR/gtsam.git
          TAG 64e2258644780eff415e1f0bac13c784c14bb92a
          NO_OVERRIDE
)
set(GTSAM_WITH_TBB OFF)
set(GTSAM_USE_SYSTEM_EIGEN ON)
set(GTSAM_POSE3_EXPMAP ON)
set(GTSAM_ROT3_EXPMAP ON)
set(GTSAM_BUILD_TESTS OFF)
set(GTSAM_BUILD_EXAMPLES_ALWAYS OFF)
configure_target(NAME GTSAM
                 VERSION 4.3.0
                 NO_FIND
)
unset(GTSAM_WITH_TBB)
unset(GTSAM_USE_SYSTEM_EIGEN)
unset(GTSAM_POSE3_EXPMAP)
unset(GTSAM_ROT3_EXPMAP)
unset(GTSAM_BUILD_TESTS)
unset(GTSAM_BUILD_EXAMPLES_ALWAYS)
