# module to build and install the filament_gldemo ILLIXR plugin

set(PLUGIN_NAME plugin.filament_gldemo${ILLIXR_BUILD_SUFFIX})

# source files, listed individually so that any changes will trigger a rebuild
add_library(${PLUGIN_NAME} SHARED plugin.cpp
        IBL.cpp
        IBL.h
        ../../include/illixr/shaders/demo_shader.hpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/error_util.hpp
        ../../include/illixr/extended_window.hpp
        ../../include/illixr/gl_util/obj.hpp
        ../../include/illixr/global_module_defs.hpp
        ../../include/illixr/math_util.hpp
        ../../include/illixr/pose_prediction.hpp
        ../../include/illixr/shader_util.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        )

target_include_directories(${PLUGIN_NAME} PRIVATE ${X11_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${GLU_INCLUDE_DIR} ${gl_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include ${Eigen3_INCLUDE_DIRS} /opt/ILLIXR/filament/include)
target_link_directories(${PLUGIN_NAME} PRIVATE /usr/lib64/ /opt/ILLIXR/filament/lib/x86_64)
target_link_libraries(${PLUGIN_NAME} PRIVATE
    assimp
    -Wl,--start-group
    filament
    backend
    bluegl
    bluevk
    filabridge
    filaflat
    utils
    geometry
    smol-v
    vkshaders
    filamat
    ibl
    filament-iblprefilter
    gltfio
    gltfio_core
    geometry
    dracodec
    camutils
    image
    ktxreader
    shaders
    stb
    viewer
    meshoptimizer
    basis_transcoder
    -Wl,--end-group
)

# Set additional linker flags
target_link_options(${PLUGIN_NAME} PRIVATE
    -static-libgcc
    -static-libstdc++
    -Wl,--gc-sections
    -fPIC
    -lc++
    -lrt
    -ldl
    -lc++abi
)
target_link_libraries(${PLUGIN_NAME} PRIVATE ${X11_LIBRARIES} ${GLEW_LIBRARIES} ${glu_LDFLAGS} ${gl_LIBRARIES}  ${Eigen3_LIBRARIES} spdlog::spdlog)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)