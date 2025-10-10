# CMake module to look for yaml-cpp
# if it is not found then it is downloaded and marked for compilation and install

pkg_check_modules(yaml-cpp QUIET yaml-cpp)
list(APPEND EXTERNAL_PROJECTS yaml-cpp)
if(yaml-cpp_FOUND)
    set(Yamlcpp_VERSION "${yaml-cpp_VERSION}")   # set current version
else()
    FetchContent_Declare(yaml-cpp
                         GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git   # Git repo for source code
                         GIT_TAG 0.8.0                                           # sha5 hash for specific commit to pull (if there is no specific tag to use)
                         GIT_PROGRESS TRUE
                         OVERRIDE_FIND_PACKAGE
    )

    set(CMAKE_BUILD_TYPE Release)
    set(BUILD_TESTING OFF)
    set(YAML_BUILD_SHARED_LIBS ON)
    FetchContent_MakeAvailable(yaml-cpp)
    set(CMAKE_BUILD_TYPE ${ILLIXR_BUILD_TYPE})
    unset(BUILD_TESTING)
    unset(YAML_BUILD_SHARED_LIBS)

    # set variables for use by modules that depend on this one
    #set(Yamlcpp_EXTERNAL Yes)      # Mark that this module is being built
    #set(yaml-cpp_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    #set(yaml-cpp_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
    #set(yaml-cpp_LIBRARIES yaml-cpp)
    find_package(yaml-cpp REQUIRED)
    add_dependencies(plugin.main${ILLIXR_BUILD_SUFFIX} cpp-yaml)
endif()

set(EXTERNAL_PROJECTS ${EXTERNAL_PROJECTS} PARENT_SCOPE)
