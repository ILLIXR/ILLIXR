vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/ILLIXR/tensorflow-lite.git
        REF 3b7e49238d61aa903cefa64721cb3a27b1472d72
)

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DCMAKE_BUILD_TYPE=Release
            -DTFLITE_ENABLE_INSTALL=ON
            -DBUILD_SHARED_LIBS OFF
            -DTFLITE_ENABLE_GPU=OFF
            -DTFLITE_ENABLE_RUY=ON
            -DTFLITE_ENABLE_NNAPI=ON
)

vcpkg_cmake_build()

vcpkg_cmake_install()
