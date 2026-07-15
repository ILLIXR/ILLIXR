vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/luxonis/depthai-core.git
        REF v2.29.0
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DBUILD_SHARED_LIBS=ON
            -DCMAKE_BUILD_TYPE=Release
)

vcpkg_cmake_build()

vcpkg_cmake_install()

vcpkg_execute_required_process(
        COMMAND rm -rf "${INSTALL_PATH}/lib/cmake/depthai/dependencies/include/spdlog"
)

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/depthai)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
