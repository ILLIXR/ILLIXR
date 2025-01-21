find_package(PkgConfig REQUIRED)
include(ExternalProject)

function(clearVars)
    set(CHECK_VARS GLSLANG_VALIDATOR_EXECUTABLE;INCLUDE_DIR;LIBRARY;GLSLC_EXECUTABLE;CFLAGS;INCLUDEDIR;INCLUDE_DIRS;LDFLAGS;LIBDIR;LIBRARIES;LIBRARY_DIRS;MODULE_NAME;PREFIX;STATIC_CFLAGS;STATIC_INCLUDE_DIRS;STATIC_LDFLAGS;STATIC_LIBRARIES;STATIC_LIBRARY_DIRS;VERSION;Headers_FOUND)
    foreach(ITEM IN LISTS CHECK_VARS)
        if(Vulkan_${ITEM})
            unset(Vulkan_${ITEM} CACHE)
            unset(Vulkan_${ITEM} PARENT_SCOPE)
        elseif (Vulkan${ITEM})
            unset(Vulkan${ITEM} CACHE)
            unset(Vulkan${ITEM} PARENT_SCOPE)
        endif()
    endforeach()
endfunction()

if(NOT Vulkan_FOUND)
    message(STATUS "Checking for module 'vulkan' > 1.3.255")
    find_package(Vulkan REQUIRED)
    if (NOT Vulkan_FOUND)
        clearVars()
        pkg_check_modules(Vulkan QUIET vulkan>=1.3.255)
        if(Vulkan_FOUND AND Vulkan_LIBRARIES AND Vulkan_LIBRARY_DIRS)
            message(Vulkan_LIBRARY_DIRS=${Vulkan_LIBRARY_DIRS})
            set(Vulkan_LIBRARIES "${Vulkan_LIBRARY_DIRS}/lib${Vulkan_LIBRARIES}.so" CACHE INTERNAL "" FORCE)
        endif()
    endif()

    # now look for glslang stuff
    if(Vulkan_FOUND)
        unset(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE CACHE)
        unset(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
	find_package(glslang REQUIRED)
        if(glslang_FOUND)
            find_program(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE glslangValidator HINTS ${CMAKE_INSTALL_PREFIX}/bin)
        else()
	    message(STATUS "glslang NOT FOUND!!!")
            set(Vulkan_FOUND FALSE CACHE INTERNAL "" FORCE)
            set(Vulkan_FOUND FALSE)
            clearVars()
        endif()
    endif()

    if(Vulkan_FOUND)
        # message(STATUS "  Found Vulkan, version ${Vulkan_VERSION} ${Vulkan_LIBRARIES} ${Vulkan_INCLUDE_DIRS}")
        # Since we now have both Vulkan and glslang found, they will use the system wide Vulkan
        # set(Vulkan_DEP_STR Vulkan_ext)
        # set(Vulkan_EXTERNAL Yes)
        set(Vulkan_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
        # set(Vulkan_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/_deps/vulkan/src/Vulkan_ext/Vulkan-Headers/include)
        set(Vulkan_LIBRARIES ${CMAKE_INSTALL_PREFIX}/lib/libvulkan.so)
        # set(Vulkan_LIBRARIES /usr/lib/x86_64-linux-gnu/libvulkan.so)
        set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE ${CMAKE_INSTALL_PREFIX}/bin/glslangValidator)
        # set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE /usr/bin/glslangValidator)
        message(STATUS "  Found Vulkan, version ${Vulkan_VERSION} ${Vulkan_LIBRARIES} ${Vulkan_INCLUDE_DIRS}")

        set(Vulkan_VERSION "${Vulkan_VERSION}" CACHE INTERNAL "" FORCE)
    else()
        message(STATUS "Building VULKAN from source")
        EXTERNALPROJECT_ADD(Vulkan_ext
                            GIT_REPOSITORY https://github.com/ILLIXR/ILLIXR-vulkan.git
                            GIT_TAG ca2e256f2ef5bcdd0ffd05f7712bd03897d9c991
                            PREFIX ${CMAKE_BINARY_DIR}/_deps/vulkan
                            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DBUILD_PARALLEL_LEVEL=${BUILD_PARALLEL_LEVEL}
                            BUILD_COMMAND cmake --build . --parallel ${BUILD_PARALLEL_LEVEL}
                            INSTALL_COMMAND ""
                            )
        set(Vulkan_DEP_STR Vulkan_ext)
        set(Vulkan_EXTERNAL Yes)
        set(Vulkan_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
        # set(Vulkan_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/_deps/vulkan/src/Vulkan_ext/Vulkan-Headers/include)
        set(Vulkan_LIBRARIES ${CMAKE_INSTALL_PREFIX}/lib/libvulkan.so)
        # set(Vulkan_LIBRARIES /usr/lib/x86_64-linux-gnu/libvulkan.so)
        set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE ${CMAKE_INSTALL_PREFIX}/bin/glslangValidator)
        # set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE /usr/bin/glslangValidator)
    endif()
endif()

set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} CACHE INTERNAL "" FORCE)
