# module to download, build and install the Monado ILLIXR_vk plugin
get_external_for_plugin(OpenXR_APP)
list(APPEND EXTERNAL_PROJECTS Monado_vk)
find_program(MONADO_VK_FOUND
             monado_vk-service
             HINTS ${CMAKE_INSTALL_PREFIX}/bin
)

find_library(MONADO_VK_OXR_LIB_FOUND
             libopenxr_monado_vk${ILLIXR_BUILD_SUFFIX}.so
             HINTS ${CMAKE_INSTALL_PREFIX}/lib
)

find_library(MONADO_LIBRARY_FOUND
             libmonado.so
             HINTS ${CMAKE_INSTALL_PREFIX}/lib
)

if (MONADO_VK_FOUND AND MONADO_VK_OXR_LIB_FOUND AND MONADO_LIBRARY_FOUND)
    set(Monado_vk_VERSION "FOUND" PARENT_SCOPE)
else ()
    fetch_git(MonadoVK
              REPO https://github.com/ILLIXR/monado_vulkan_integration.git
              TAG b4d67519ec3e4a5e0af7038bed62740fbf08712c
    )

    set(ILLIXR_PATH ${CMAKE_SOURCE_DIR}/include)
    set(ON_VARS XRT_HAVE_LIBUDEV XRT_HAVE_LIBUSB XRT_HAVE_V4L2 XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH XRT_FEATURE_SERVICE)
    set(OFF_VARS XRT_HAVE_LIBUVC XRT_HAVE_SDL2 XRT_BUILD_DRIVER_ANDROID XRT_BUILD_DRIVER_ARDUINO XRT_BUILD_DRIVER_DAYDREAM
        XRT_BUILD_DRIVER_DEPTHAI XRT_BUILD_DRIVER_EUROC XRT_BUILD_DRIVER_HANDTRACKING XRT_BUILD_DRIVER_HDK
        XRT_BUILD_DRIVER_HYDRA XRT_BUILD_DRIVER_NS XRT_BUILD_DRIVER_VIVE XRT_BUILD_DRIVER_HANDTRACKING XRT_BUILD_DRIVER_WMR
        XRT_BUILD_DRIVER_SIMULAVR XRT_BUILD_DRIVER_SIMULATED XRT_BUILD_SAMPLES XRT_FEATURE_TRACING XRT_FEATURE_WINDOW_PEEK
        XRT_BUILD_DRIVER_OHMD XRT_BUILD_DRIVER_OPENGLOVES XRT_BUILD_DRIVER_PSMV XRT_BUILD_DRIVER_PSVR XRT_BUILD_DRIVER_QWERTY
        XRT_BUILD_DRIVER_REALSENSE XRT_BUILD_DRIVER_REMOTE XRT_BUILD_DRIVER_RIFT_S XRT_BUILD_DRIVER_SURVIVE
        XRT_BUILD_DRIVER_ULV2 XRT_BUILD_DRIVER_VF)

    foreach(ITEM IN LISTS ON_VARS)
        set(${ITEM} ON)
    endforeach()
    foreach(ITEM IN LISTS OFF_VARS)
        set(${ITEM} OFF)
    endforeach()
    configure_target(NAME Monado_VK
                     NO_FIND
    )

    foreach(ITEM IN LISTS ON_VARS OFF_VARS)
        unset(${ITEM})
    endforeach()
    unset(ILLIXR_PATH)


                        DEPENDS ${Vulkan_DEP_STR}                              # dependencies of this module
                        #arguments to pass to CMake

                        # custom install command to get the name of the plugin correct
                        INSTALL_COMMAND cmake --install ./ && ln -sf ${CMAKE_INSTALL_PREFIX}/lib/libopenxr_monado_vk.so ${CMAKE_INSTALL_PREFIX}/lib/libopenxr_monado_vk${ILLIXR_BUILD_SUFFIX}.so


endif ()
set(MONADO_RUNTIME "${CMAKE_INSTALL_PREFIX}/share/openxr/1/openxr_monado_vk.json" PARENT_SCOPE)

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
