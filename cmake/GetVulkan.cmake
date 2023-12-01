find_package(Vulkan 1.3.255 QUIET)

if (NOT Vulkan_FOUND)
    set(CHECK_VARS Vulkan_GLSLANG_VALIDATOR_EXECUTABLE;Vulkan_INCLUDE_DIR;Vulkan_INCLUDE_DIRS;Vulkan_LIBRARIES;Vulkan_LIBRARY;Vulkan_GLSLC_EXECUTABLE)
    foreach(ITEM IN LISTS CHECK_VARS)
        if(${ITEM})
            unset(${ITEM} CACHE)
            unset(${ITEM})
        endif()
    endforeach()
    unset(CHECK_VARS)
    pkg_check_modules(Vulkan QUIET vulkan>=1.3.255)
    find_package(glslang 13.1.1 REQUIRED)
    find_program(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE glslangValidator HINTS ${CMAKE_INSTALL_PREFIX}/bin REQUIRED)
endif()

if(Vulkan_FOUND)
    set(Vulkan_VERSION "${Vulkan_VERSION}" PARENT_SCOPE)
else()
    EXTERNALPROJECT_ADD(Vulkan_ext
                        GIT_REPOSITORY https://github/com/ILLIXR/ILLIXR-vulkan.git
                        GIT_TAG 8f85e93cd10d9a6cd6ab4f98603797a857fbb5e5
                        PREFIX ${CMAKE_BINARY_DIR}/_deps/vulkan
                        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release
                        )
    set(Vulkan_DEP_STR Vulkan_ext)
    set(Vulkan_EXTERNAL Yes)
    set(Vulkan_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(Vulkan_LIBRARIES ${CMAKE_INSTALL_PREFIX}/lib/libvulkan.so)
endif()