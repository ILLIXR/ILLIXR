vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/ILLIXR/draco_illixr.git
        REF 4dae9f429fa4c98aab907350de7e8d8c2c878817
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DDRACO_TRANSCODER_SUPPORTED=ON
            -DCMAKE_BUILD_TYPE=Release
)

vcpkg_cmake_build()

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/draco_illixr)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
