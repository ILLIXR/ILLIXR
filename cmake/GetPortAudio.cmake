pkg_check_modules(PORTAUDIO QUIET portaudio-2.0>=19)
if (PORTAUDIO_FOUND)
    set(PortAudio_VERSION "${PORTAUDIO_VERSION}")
else()
    EXTERNALPROJECT_ADD(PortAudio
            GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
            GIT_TAG 7e2a33c875c6b2b53a8925959496cc698765621f
            PREFIX ${CMAKE_BINARY_DIR}/_deps/portaudio
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release
            )
    set(PortAudio_DEP_STR "PortAudio")
    set(PortAudio_EXTERNAL Yes)
endif()
