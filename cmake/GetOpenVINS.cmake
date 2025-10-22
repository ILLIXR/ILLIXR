FetchContent_Declare(OpenVINS
                     GIT_REPOSITORY https://github.com/ILLIXR/open_vins.git   # Git repo for source code
                     GIT_TAG 23894d601bfc1e1fa064239916cf2276e00b9ca2         # sha5 hash for specific commit to pull (if there is no specific tag to use)
)
set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)
set(ILLIXR_INTEGRATION ON)
FetchContent_MakeAvailable(OpenVINS)
unset(ILLIXR_INTEGRATION)
