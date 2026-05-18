# module to download, build and install the ORM_SLAM ILLIXR plugin

# get dependencies
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0>=19)
pkg_check_modules(SPATIALAUDIO REQUIRED spatialaudio)

fetch_git(NAME Audio_Pipeline
          REPO https://github.com/ILLIXR/audio_pipeline.git
          TAG 4587b5e4a9f9a01d6ae85a9381b4e8e22b7c883c
)

set(ILLIXR_INTEGRATION ON)
configure_target(NAME Audio_Pipeline
                 NO_FIND
                 MATCH_BUILD_TYPE
)
unset(ILLIXR_INTEGRATION)
