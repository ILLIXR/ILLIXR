# CMake module to look for yaml-cpp
# if it is not found then it is downloaded and marked for compilation and install
find_package(yaml-cpp QUIET CONFIG)
if(NOT yaml-cpp_FOUND)
    pkg_check_modules(yaml-cpp QUIET yaml-cpp)
endif()

if(yaml-cpp_FOUND)
    set(Yamlcpp_VERSION "${yaml-cpp_VERSION}")   # set current version
else()
    EXTERNALPROJECT_ADD(cpp-yaml
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git   # Git repo for source code
            GIT_TAG 0.8.0                                           # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/yaml-cpp               # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${DEPENDENCY_BUILD_TYPE} -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF -DYAML_BUILD_SHARED_LIBS=ON
            )
    # set variables for use by modules that depend on this one
    set(Yamlcpp_EXTERNAL Yes)      # Mark that this module is being built
    set(yaml-cpp_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(yaml-cpp_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
    set(yaml-cpp_LIBRARIES "yaml-cpp${DEPENDENCY_BUILD_SUFFIX}")
    add_dependencies(plugin.main${ILLIXR_BUILD_SUFFIX} cpp-yaml)
endif()
