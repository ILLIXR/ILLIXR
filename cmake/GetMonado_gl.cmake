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
        GIT_TAG c99cbcbadffeebb730e08fd7e650ff84d5817df7       # sha5 hash for specific commit to pull (if there is no specific tag to use)
        PREFIX ${CMAKE_BINARY_DIR}/_deps/monado                # the build directory
        DEPENDS ${OpenCV_DEP_STR}                              # dependencies of this module
        #arguments to pass to CMake
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DBUILD_TESTING=OFF -DILLIXR_PATH=${CMAKE_SOURCE_DIR}/include -DXRT_HAVE_LIBUDEV=ON -DXRT_HAVE_LIBUSB=ON -DXRT_HAVE_LIBUVC=ON -DXRT_HAVE_V4L2=ON -DXRT_HAVE_SDL2=OFF -DXRT_BUILD_DRIVER_ANDROID=OFF -DXRT_BUILD_DRIVER_ARDUINO=OFF -DXRT_BUILD_DRIVER_DAYDREAM=OFF -DXRT_BUILD_DRIVER_DEPTHAI=OFF -DXRT_BUILD_DRIVER_EUROC=OFF -DXRT_BUILD_DRIVER_HANDTRACKING=OFF -DXRT_BUILD_DRIVER_HDK=OFF -DXRT_BUILD_DRIVER_HYDRA=OFF -DXRT_BUILD_DRIVER_NS=OFF -DXRT_BUILD_DRIVER_OHMD=OFF -DXRT_BUILD_DRIVER_OPENGLOVES=OFF -DXRT_BUILD_DRIVER_PSMV=OFF -DXRT_BUILD_DRIVER_PSVR=OFF -DXRT_BUILD_DRIVER_QWERTY=OFF -DXRT_BUILD_DRIVER_REALSENSE=OFF -DXRT_BUILD_DRIVER_REMOTE=OFF -DXRT_BUILD_DRIVER_RIFT_S=OFF -DXRT_BUILD_DRIVER_SURVIVE=OFF -DXRT_BUILD_DRIVER_ULV2=OFF -DXRT_BUILD_DRIVER_VF=OFF -DXRT_BUILD_DRIVER_VIVE=OFF -DXRT_BUILD_DRIVER_HANDTRACKING=OFF -DXRT_BUILD_DRIVER_WMR=OFF -DXRT_BUILD_DRIVER_SIMULAVR=OFF -DXRT_BUILD_DRIVER_SIMULATED=OFF -DXRT_BUILD_SAMPLES=OFF -DXRT_FEATURE_TRACING=OFF -DXRT_FEATURE_SERVICE=ON -DXRT_FEATURE_WINDOW_PEEK=OFF -DCMAKE_INSTALL_LIBDIR=lib ${MONADO_CMAKE_ARGS}
        # custom install command to get the name of the plugin correct
        INSTALL_COMMAND cmake --install ./ && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libopenxr_monado_gl.so ${CMAKE_INSTALL_PREFIX}/lib/libopenxr_monado_gl${ILLIXR_BUILD_SUFFIX}.so
        )
set(Monado_gl_EXTERNAL YES)
set(Monado_gl_DEP_STR MonadoGL)
set(MONADO_RUNTIME_gl "${CMAKE_INSTALL_PREFIX}/share/openxr/1/openxr_monado_gl.json" PARENT_SCOPE)
