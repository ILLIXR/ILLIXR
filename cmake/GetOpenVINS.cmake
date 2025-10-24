FetchContent_Declare(OpenVINS
                     GIT_REPOSITORY https://github.com/ILLIXR/open_vins.git   # Git repo for source code
                     GIT_TAG 4976aa6426e01dad6115d09ad575d84a9ae575da         # sha5 hash for specific commit to pull (if there is no specific tag to use)
)
set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)
set(ILLIXR_INTEGRATION ON)
FetchContent_MakeAvailable(OpenVINS)
unset(ILLIXR_INTEGRATION)
