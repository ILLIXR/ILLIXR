pkg_check_modules(glslang REQUIRED glslang)
find_package(gflags REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(TIFF REQUIRED)
pkg_check_modules(udev REQUIRED udev)
pkg_check_modules(wayland-server REQUIRED wayland-server)
pkg_check_modules(x11-xcb REQUIRED x11-xcb)
pkg_check_modules(xcb-glx REQUIRED xcb-glx)
pkg_check_modules(xcb-randr REQUIRED xcb-randr)
pkg_check_modules(xkbcommon REQUIRED xkbcommon)
pkg_check_modules(xrandr REQUIRED xrandr)
find_package(OpenXR REQUIRED)
find_package(Vulkan REQUIRED)

pkg_check_modules(libusb-1.0 REQUIRED libusb-1.0)

EXTERNALPROJECT_ADD(Monado
        GIT_REPOSITORY https://github.com/ILLIXR/monado_integration.git
        GIT_TAG faed7f66a1b69c7a92b671060dae033028c1addb
        PREFIX ${CMAKE_BINARY_DIR}/_deps/monado
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_PATH=${CMAKE_SOURCE_DIR}/runtime -DBUILD_WITH_LIBUDEV=0 -DBUILD_WITH_LIBUSB=0 -DBUILD_WITH_LIBUVC=0 -DBUILD_WITH_NS=0 -DBUILD_WITH_PSMV=0 -DBUILD_WITH_PSVR=0 -DBUILD_WITH_OPENHMD=0 -DBUILD_WITH_VIVE=0
        )
