vcpkg_from_git(
        OUT_SOURCE_PATH SOURCE_PATH
        URL https://github.com/collabora/libsurvive.git
        REF 4fb6d888d0277a8a3ba725e63707434d80ecdb2a
        PATCHES 
            fix-c-standard.patch
            fix-redist-build.patch
            fix-nuget-windows.patch
            fix-windows-export.patch
            fix-export.patch
            fix-pkg-config.patch
)

# Fetch submodules and place them in the correct subdirectories
vcpkg_from_git(
        OUT_SOURCE_PATH SURVIVE_SUBMODULE_CNKALMAN
        URL https://github.com/collabora/cnkalman.git
        REF 6b350314225e28d2e4e8daad7d2971d22386f76f
        PATCHES fix-cnkalman-subproject.patch
)

vcpkg_from_git(
        OUT_SOURCE_PATH CNKALMAN_SUBMODULE_CNMATRIX
        URL https://github.com/collabora/cnmatrix.git
        REF 18407cb6866b7235369e4d713f3eb3b0aafdf200
        PATCHES fix-cnmatrix-subproject.patch
)

file(COPY "${CNKALMAN_SUBMODULE_CNMATRIX}/" DESTINATION "${SURVIVE_SUBMODULE_CNKALMAN}/libs/cnmatrix")

file(COPY "${SURVIVE_SUBMODULE_CNKALMAN}/" DESTINATION "${SOURCE_PATH}/libs/cnkalman")

vcpkg_cmake_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            -DCMAKE_CXX_STANDARD=17
            -DCMAKE_CXX_STANDARD_REQUIRED=ON
            -DCMAKE_C_STANDARD=99
            -DDOWNLOAD_EIGEN=OFF
            -DENABLE_TESTS=OFF
            -DBUILD_APPLICATIONS=OFF
            -DUSE_EIGEN=ON
            -DUSE_OPENBLAS=OFF
            -DBUILD_APPLICATIONS=OFF
)

vcpkg_cmake_build()

vcpkg_cmake_install()

vcpkg_fixup_pkgconfig()

file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/src/libsurvive.lib"
     DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/src/libsurvive.lib"
     DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/libsurvive.dll"
     DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")

file(COPY "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/libsurvive.dll"
     DESTINATION "${CURRENT_PACKAGES_DIR}/bin")