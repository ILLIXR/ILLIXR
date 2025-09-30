find_package(draco_illixr QUIET CONFIG)
if (NOT draco_illixr_FOUND)
    FetchContent_Declare(Draco_ILLIXR
                         GIT_REPOSITORY https://github.com/ILLIXR/draco.git
                         GIT_TAG bba2a71ae3d46631a3b6d969e60730d570e904aa
    )
    set(TEMP_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    set(DRACO_TRANSCODER_SUPPORTED ON)
    set(CMAKE_BUILD_TYPE=Release)
    FetchContent_MakeAvailable(Draco_ILLIXR)
    set(CMAKE_BUILD_TYPE ${TEMP_BUILD_TYPE})
    uset(DRACO_TRANSCODER_SUPPORT)
     	       

#[[
    set(Draco_DEP_STR Draco_ext)
    set(Draco_EXTERNAL Yes)
    add_library(draco_illixr::draco STATIC IMPORTED)
    set_target_properties(draco_illixr::draco PROPERTIES
                          INTERFACE_COMPILE_FEATURES "cxx_std_11"
                          INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/include"
    )
    set_target_properties(draco_illixr::draco PROPERTIES
                          IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
                          IMPORTED_LOCATION "${CMAKE_INSTALL_PREFIX}/lib/libdraco_illixr.a"
    )
]]
else()
    message(STATUS "  Found draco_illixr, ${draco_illixr_VERSION}")
endif()
