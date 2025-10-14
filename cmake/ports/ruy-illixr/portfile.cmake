vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/ILLIXR/ruy.git
        REF dadeec28688d203bba1ea10ec9d331204dc6c28c
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
        -DCMAKE_BUILD_TYPE=Release
)

vcpkg_cmake_build()

vcpkg_cmake_install()
