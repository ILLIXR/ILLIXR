vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/ILLIXR/gtsam.git
        REF 135f09fe08f749596a03d4d018387f4590f826c1
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DGTSAM_WITH_TBB=OFF
            -DGTSAM_USE_SYSTEM_EIGEN=ON
            -DGTSAM_POSE3_EXPMAP=ON
            -DGTSAM_ROT3_EXPMAP=ON
            -DGTSAM_BUILD_TESTS=OFF
            -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF
            -DCMAKE_BUILD_TYPE=Release
)

vcpkg_cmake_build()

vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
