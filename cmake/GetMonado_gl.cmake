# module to download, build and install the Monado_gl ILLIXR plugin

# get dependencies
pkg_check_modules(glslang glslang)
if(NOT glslang_FOUND)
    find_package(glslang 11 REQUIRED)
endif()
#pkg_check_modules(glslang REQUIRED glslang)
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
get_external_for_plugin(OpenXR_APP)
pkg_check_modules(Vulkan vulkan)
if(NOT Vulkan_FOUND)
    find_package(Vulkan REQUIRED)
endif()
pkg_check_modules(libusb-1.0 REQUIRED libusb-1.0)

set(MONADO_CMAKE_ARGS "")

# if building on CentOS make sure we use the correct OpenCV
if(HAVE_CENTOS)
    set(MONADO_CMAKE_ARGS "-DINTERNAL_OPENCV=${OpenCV_DIR}")
endif()
EXTERNALPROJECT_ADD(MonadoGL
        GIT_REPOSITORY https://github.com/ILLIXR/monado_integration.git   # Git repo for source code
        GIT_TAG c2e5fc808717f6d1f05cc1813888752c6e8bf6ef       # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${CMAKE_BINARY_DIR}/_deps/monado                # the build directory
        DEPENDS ${OpenCV_DEP_STR}                              # dependencies of this module
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DILLIXR_PATH=${CMAKE_SOURCE_DIR}/include -DBUILD_WITH_LIBUDEV=0 -DBUILD_WITH_LIBUSB=0 -DBUILD_WITH_LIBUVC=0 -DBUILD_WITH_NS=0 -DBUILD_WITH_PSMV=0 -DBUILD_WITH_PSVR=0 -DBUILD_WITH_OPENHMD=0 -DBUILD_WITH_VIVE=0 -DCMAKE_INSTALL_LIBDIR=lib ${MONADO_CMAKE_ARGS}
        # custom install command to get the name of the plugin correct
        INSTALL_COMMAND make install && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libopenxr_monado_gl.so ${CMAKE_INSTALL_PREFIX}/lib/libopenxr_monado_gl${ILLIXR_BUILD_SUFFIX}.so
        )
set(Monado_gl_EXTERNAL YES)
set(Monado_gl_DEP_STR MonadoGL)
set(MONADO_RUNTIME_GL "${CMAKE_INSTALL_PREFIX}/share/openxr/1/openxr_monado_gl.json" PARENT_SCOPE)
