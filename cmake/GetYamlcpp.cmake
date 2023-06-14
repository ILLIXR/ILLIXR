# CMake module to look for yaml-cpp
# if it is not found then it is downloaded and marked for compilation and install

pkg_check_modules(yaml-cpp QUIET yaml-cpp)

if(yaml-cpp_FOUND)
    set(Yamlcpp_VERSION "${yaml-cpp_VERSION}")   # set current version
else()
    EXTERNALPROJECT_ADD(cpp-yaml
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git   # Git repo for source code
            GIT_TAG 0579ae3d976091d7d664aa9d2527e0d0cff25763        # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/yaml-cpp               # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF -DYAML_BUILD_SHARED_LIBS=ON
            )
    # set variables for use by modules that depend on this one
    set(Yamlcpp_EXTERNAL Yes)      # Mark that this module is being built
    set(yaml-cpp_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(yaml-cpp_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
    set(yaml-cpp_LIBRARIES yaml-cpp)
    add_dependencies(main${ILLIXR_BUILD_SUFFIX}.exe cpp-yaml)
endif()
