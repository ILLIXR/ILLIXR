vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO borglab/gtsam
        REF 4.3a0
        SHA512 f1dae95c3515d43006da9fd0c60723d7d00b54a889fe26aeaa9e4b346fceb0d4033a861026e0099b5644e2314acb23ebbc9d4fd8638c7d132aa6a31e0915b9dd
        PATCHES
            fix-version.patch
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DGTSAM_BUILD_TESTS=OFF
            -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF
            -DGTSAM_BUILD_DOCS=OFF
            -DGTSAM_USE_BOOST_FEATURES=OFF
            -DGTSAM_ENABLE_BOOST_SERIALIZATION=OFF
            -DGTSAM_ALLOW_DEPRECATED_SINCE_V43=ON
            -DGTSAM_WITH_TBB=OFF
            -DGTSAM_USE_SYSTEM_EIGEN=ON
            -DGTSAM_POSE3_EXPMAP=ON
            -DGTSAM_ROT3_EXPMAP=ON
            -DGTSAM_BUILD_UNSTABLE=ON
)
vcpkg_cmake_build()
vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME GTSAM CONFIG_PATH CMake)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")