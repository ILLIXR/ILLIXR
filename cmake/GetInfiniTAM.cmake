get_external_for_plugin(Draco)

fetch_git(NAME InfiniTAM_ext
          REPO https://github.com/ILLIXR/InfiniTAM.git
          TAG dc3c2841a6137c05cf0aef52f1d051c86f3f7b8b
)
set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)
configure_target(NAME InfiniTAM_ext)

if(TARGET draco_static)
    add_dependencies(plugin.ada.infinitam${ILLIXR_BUILD_SUFFIX} draco_static)
    target_include_directories(plugin.ada.infinitam${ILLIXR_BUILD_SUFFIX} PUBLIC ${draco_illixr_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
endif()
