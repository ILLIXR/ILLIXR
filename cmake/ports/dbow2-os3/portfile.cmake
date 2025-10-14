vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/ILLIXR/DBoW2_OS3.git
        REF 3cb52aa1162cd07354f75512454b0dea75cce7c1
)

vcpkg_cmake_configure()

vcpkg_cmake_build()

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/DBoW2_OS3)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
