if (NOT Infinitam_FOUND)
    FetchContent_Declare(InfiniTAM_ext
                         GIT_REPOSITORY https://github.com/ILLIXR/InfiniTAM.git
                         GIT_TAG 40ec1705f169b2eddb97e4c2d983d099518ef8bc
    )
    set(ILLIXR_ROOT ${CMAKE_SOURCE_DIR}/include)

    FetchContent_MakeAvailable(InfiniTAM_ext)
endif()
