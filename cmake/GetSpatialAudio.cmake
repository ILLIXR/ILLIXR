pkg_check_modules(SPATIALAUDIO QUIET spatialaudio)

if(SPATIALAUDIO_FOUND)
    set(SpatialAudio_VERSION "${SPATIALAUDIO_VERSION}")
else()
    EXTERNALPROJECT_ADD(SpatialAudio
            GIT_REPOSITORY https://github.com/ILLIXR/libspatialaudio.git
            GIT_TAG 77a901e337a63aa981745ab369ccf597834a37a5
            PREFIX ${CMAKE_BINARY_DIR}/_deps/spatialaudio
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib
            )
    set(SpatialAudio_DEP_STR "SpatialAudio")
    set(SpatialAudio_EXTERNAL Yes)
endif()
