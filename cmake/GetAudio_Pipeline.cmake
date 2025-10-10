# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0>=19)
pkg_check_modules(SPATIALAUDIO REQUIRED spatialaudio)

fetch_git(NAME Audio_Pipeline
          REPO https://github.com/ILLIXR/audio_pipeline.git
          TAG f2603d835005250652634f7f25466e51d1b72892
)

set(ILLIXR_INTEGRATION ON)
configure_target(NAME Audio_Pipeline
                 NO_FIND
                 MATCH_BUILD_TYPE
)
unset(ILLIXR_INTEGRATION)
