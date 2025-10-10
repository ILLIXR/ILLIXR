list(APPEND EXTERNAL_PROJECTS Vulkan)

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


message(STATUS "Checking for module 'vulkan' > 1.4")
find_package(Vulkan 1.4 QUIET)
if(NOT Vulkan_FOUND)
    clearVars()
    pkg_check_modules(Vulkan QUIET vulkan>=1.4)
    if(Vulkan_FOUND AND Vulkan_LIBRARY_DIRS)
        set(Vulkan_LIBRARIES "${Vulkan_LIBRARY_DIRS}/lib${Vulkan_LIBRARIES}.so" CACHE INTERNAL "" FORCE)
    endif()
endif()

if(Vulkan_FOUND)
    find_package(glslang QUIET)
    if (NOT glslang_FOUND)
        pkg_check_modules(glslang glslang)
    endif()
    if(glslang_FOUND)
        if (NOT Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
            find_program(GLSLANG_VALIDATOR_EXECUTABLE_TEMP glslangValidator)
            if (NOT GLSLANG_VALIDATOR_EXECUTABLE_TEMP)
                set(Vulkan_FOUND FALSE CACHE INTERNAL "" FORCE)
                set(Vulkan_FOUND FALSE)
                clearVars()
                message(STATUS "Cannot find glslangValidator!")
            else()
                set(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE "${GLSLANG_VALIDATOR_EXECUTABLE_TEMP}")
            endif()
        else()
            message(STATUS "glslangValidator is found at ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
        endif()
    else()
        message(STATUS "glslang NOT FOUND!!!")
        set(Vulkan_FOUND FALSE CACHE INTERNAL "" FORCE)
        set(Vulkan_FOUND FALSE)
        clearVars()
    endif()
endif()

if (Vulkan_FOUND)
    set(Vulkan_VERSION ${Vulkan_VERSION} PARENT_SCOPE)
endif()

if (NOT Vulkan_FOUND)
    fetch_git(NAME Vulkan
              REPOSITORY https://github.com/ILLIXR/ILLIXR-vulkan.git
              TAG 7901de80434662709e0357d1eac39376055b0b79
    )

    configure_target(NAME Vulkan
                     VERSION 1.4
    )
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
