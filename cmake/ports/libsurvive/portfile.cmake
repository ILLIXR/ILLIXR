vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/collabora/libsurvive.git
        REF 4fb6d888d0277a8a3ba725e63707434d80ecdb2a
)

vcpkg_cmake_configure()

vcpkg_cmake_build()

vcpkg_cmake_install()
