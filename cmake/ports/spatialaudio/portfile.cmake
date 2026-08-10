vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/ILLIXR/libspatialaudio.git
        REF 12a48a20e45d9a7203d49821e2c4f253c8f933b7
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DCMAKE_BUILD_TYPE=Release
)

vcpkg_cmake_build()

vcpkg_cmake_install()
