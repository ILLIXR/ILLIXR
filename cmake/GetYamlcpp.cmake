pkg_check_modules(yaml-cpp QUIET yaml-cpp)

if(NOT yaml-cpp_FOUND)
    EXTERNALPROJECT_ADD(cpp-yaml
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0579ae3d976091d7d664aa9d2527e0d0cff25763
            PREFIX ${CMAKE_BINARY_DIR}/_deps/yaml-cpp
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=OFF -DYAML_BUILD_SHARED_LIBS=ON
            )
    set(SpatialAudio_DEP_STR "SpatialAudio")
    set(yaml-cpp_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)
    set(yaml-cpp_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
    set(yaml-cpp_LIBRARIES yaml-cpp)
    add_dependencies(main${ILLIXR_BUILD_SUFFIX}.exe cpp-yaml)
endif()
