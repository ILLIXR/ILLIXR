add_library(illixr_hmd OBJECT
            hmd.cpp
            hmd.hpp
)

target_include_directories(illixr_hmd PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_library(illixr_hmd_gl OBJECT
            hmd.cpp
            hmd.hpp
)

target_include_directories(illixr_hmd_gl PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(illixr_hmd_gl PUBLIC USE_GL)
