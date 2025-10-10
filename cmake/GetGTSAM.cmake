# CMake module to look for GTSAM
# if it is not found then it is downloaded and marked for compilation and install

find_package(GTSAM 4.3.0 QUIET EXACT)
find_package(GTSAM_UNSTABLE 4.3.0 QUIET EXACT)
list(APPEND EXTERNAL_PROJECTS GTSAM)
if(NOT GTSAM_FOUND AND NOT GTSAM_UNSTABLE_FOUND)
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
                     VERSION 4.3.0
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

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
