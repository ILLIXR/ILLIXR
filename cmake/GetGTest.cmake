# CMake module to fetch and build GoogleTest from source
#
# Unlike other third-party dependencies, GoogleTest is always built from source here rather than
# checked via find_package() first. The unit test infrastructure relies on every unit test library
# linking against the exact same GoogleTest shared library instance, so a system-installed GTest
# built with different BUILD_SHARED_LIBS or CRT settings would silently break that assumption.
# Building from one pinned tag guarantees a consistent ABI across Linux, Windows, and Android.

fetch_git(NAME illixr_googletest
          REPO https://github.com/google/googletest.git
          TAG v1.17.0
)

set(BUILD_SHARED_LIBS ON)
set(INSTALL_GTEST OFF)
set(GTEST_HAS_ABSL OFF)
if(WIN32 OR MSVC)
    set(gtest_force_shared_crt ON)
endif()

configure_target(NAME illixr_googletest
                 NO_FIND
)

set_target_properties(gtest PROPERTIES OUTPUT_NAME illixr_gtest)
set_target_properties(gtest_main PROPERTIES OUTPUT_NAME illixr_gtest_main)
set_target_properties(gmock PROPERTIES OUTPUT_NAME illixr_gmock)
set_target_properties(gmock_main PROPERTIES OUTPUT_NAME illixr_gmock_main)

unset(BUILD_SHARED_LIBS)
unset(INSTALL_GTEST)
unset(GTEST_HAS_ABSL)
if(WIN32 OR MSVC)
    unset(gtest_force_shared_crt)
endif()
