vcpkg_download_distfile(
        ARCHIVE
        URLS "https://storage.googleapis.com/mirror.tensorflow.org/github.com/petewarden/OouraFFT/archive/v1.0.tar.gz"
        FILENAME "v1.0.tar.gz"
        SHA512 5f4dabc2ae21e1f537425d58a49cdca1c49ea11db0d6271e2a4b27e9697548eb
)

vcpkg_extract_source_archive(
        SOURCE_PATH
        ARCHIVE "${ARCHIVE}"
        PATCHES
            fft2d.patch
)

vcpkg_cmake_build()

vcpkg_cmake_install()
