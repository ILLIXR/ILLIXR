# CMake module to look for Portaudio
# if it is not found then it is downloaded and marked for compilation and install

pkg_check_modules(PORTAUDIO QUIET portaudio-2.0>=19)

if (PORTAUDIO_FOUND)
    set(PortAudio_VERSION "${PORTAUDIO_VERSION}")   # set current version
else()
    EXTERNALPROJECT_ADD(PortAudio
            GIT_REPOSITORY https://github.com/PortAudio/portaudio.git   # Git repo for source code
            GIT_TAG 7e2a33c875c6b2b53a8925959496cc698765621f            # sha5 hash for specific commit to pull (if there is no specific tag to use)
            PREFIX ${CMAKE_BINARY_DIR}/_deps/portaudio                  # the build directory
            # arguments to pass to CMake
            CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=Release
            )
    # set variables for use by modules that depend on this one
    set(PortAudio_DEP_STR "PortAudio")   # Dependency string for other modules that depend on this one
    set(PortAudio_EXTERNAL Yes)      # Mark that this module is being built
endif()
