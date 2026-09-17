cmake_minimum_required(VERSION 3.22)
project(boba_frame_checks LANGUAGES CXX)

enable_testing()
get_filename_component(ILLIXR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(keyframe_gate keyframe_gate.cpp)
target_compile_features(keyframe_gate PRIVATE cxx_std_17)
target_include_directories(keyframe_gate PRIVATE "${ILLIXR_ROOT}")
add_test(NAME boba_keyframe_recovery COMMAND keyframe_gate)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME boba_hand_input
         COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/hand_input.py" --cxx "${CMAKE_CXX_COMPILER}")

find_package(Eigen3 REQUIRED)
find_path(OPENXR_INCLUDE_DIR openxr/openxr.h REQUIRED)
add_executable(hand_mesh hand_mesh.cpp)
target_compile_features(hand_mesh PRIVATE cxx_std_17)
target_include_directories(hand_mesh PRIVATE "${ILLIXR_ROOT}" "${OPENXR_INCLUDE_DIR}")
target_link_libraries(hand_mesh PRIVATE Eigen3::Eigen)
# These checks use assertions even when validating a Release build.
target_compile_options(hand_mesh PRIVATE -UNDEBUG)
add_test(NAME boba_hand_mesh COMMAND hand_mesh)

# Optional hardware regression against a built boba_streaming_server plugin.
set(BOBA_NVENC_LIBRARY "" CACHE FILEPATH "Built libplugin.boba_streaming_server.opt.so")
if(BOBA_NVENC_LIBRARY)
    find_package(CUDAToolkit REQUIRED)
    find_package(spdlog REQUIRED)
    find_path(NVENC_INCLUDE_DIR nvEncodeAPI.h REQUIRED
              HINTS "${VIDEO_CODEC_SDK_PATH}/Interface" "${VIDEO_CODEC_SDK_PATH}/include/ffnvcodec")
    add_executable(nvenc_ring_generation nvenc_ring_generation.cpp)
    target_compile_features(nvenc_ring_generation PRIVATE cxx_std_17)
    target_compile_definitions(nvenc_ring_generation PRIVATE
                              NDEBUG FMT_HEADER_ONLY USE_AV1 COMBINED_ENCODING ILLIXR_BOBA_ENCODER)
    target_include_directories(nvenc_ring_generation PRIVATE
                               "${ILLIXR_ROOT}/plugins" "${ILLIXR_ROOT}/include" "${NVENC_INCLUDE_DIR}")
    target_link_libraries(nvenc_ring_generation PRIVATE
                          "${BOBA_NVENC_LIBRARY}" spdlog::spdlog CUDA::cudart CUDA::cuda_driver)
    add_test(NAME boba_nvenc_ring_generation
             COMMAND nvenc_ring_generation "${CMAKE_CURRENT_BINARY_DIR}/stereo.obu")
endif()
