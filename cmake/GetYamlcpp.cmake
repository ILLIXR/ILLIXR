# CMake module to look for yaml-cpp
# if it is not found then it is downloaded and marked for compilation and install

pkg_check_modules(yaml-cpp QUIET yaml-cpp)
list(APPEND EXTERNAL_PROJECTS yaml-cpp)
if(yaml-cpp_FOUND)
    set(Yamlcpp_VERSION "${yaml-cpp_VERSION}")   # set current version
else()
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "yaml-cpp should be installed with vcpkg")
    endif()
    fetch_git(NAME yaml-cpp
              REPO https://github.com/jbeder/yaml-cpp.git
              TAG 0.8.0
    )

    set(BUILD_TESTING OFF)
    set(YAML_BUILD_SHARED_LIBS ON)
    configure_target(NAME yaml-cpp)
    unset(BUILD_TESTING)
    unset(YAML_BUILD_SHARED_LIBS)

    find_package(yaml-cpp REQUIRED)
    add_dependencies(plugin.main${ILLIXR_BUILD_SUFFIX} cpp-yaml)
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS})
