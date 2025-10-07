get_external_for_plugin(Draco)
if (NOT Infinitam_FOUND)
    message(STATUS "Downloading InfiniTAM")
    FetchContent_Declare(InfiniTAM_ext
                         GIT_REPOSITORY https://github.com/ILLIXR/InfiniTAM.git
                         GIT_TAG dc3c2841a6137c05cf0aef52f1d051c86f3f7b8b
    )
    set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)

    FetchContent_MakeAvailable(InfiniTAM_ext)
    if(TARGET draco_static)
        add_dependencies(plugin.ada.infinitam${ILLIXR_BUILD_SUFFIX} draco_static)
        target_include_directories(plugin.ada.infinitam${ILLIXR_BUILD_SUFFIX} PUBLIC ${draco_illixr_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
    endif()
endif()
