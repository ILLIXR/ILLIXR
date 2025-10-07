# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0>=19)
pkg_check_modules(SPATIALAUDIO REQUIRED spatialaudio)

FetchContent_Declare(Audio_Pipeline
                     GIT_REPOSITORY https://github.com/ILLIXR/audio_pipeline.git   # Git repo for source code
                     GIT_TAG f2603d835005250652634f7f25466e51d1b72892              # sha5 hash for specific commit to pull (if there is no specific tag to use)
)

set(ILLIXR_INTEGRATION ON)
FetchContent_MakeAvailable(Audio_Pipeline)
unset(ILLIXR_INTEGRATION)
