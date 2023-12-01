cmake_minimum_required(VERSION 3.16)

project(offload_rendering_client)

set(PLUGIN_NAME plugin.offload_rendering_client${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_external_for_plugin(Vulkan)
if(USE_MONADO)
    get_external_for_plugin(Monado_vk)
endif()

# ffmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice libavformat libavcodec libavutil libswscale)

find_package(CUDAToolkit REQUIRED)

add_library(${PLUGIN_NAME} SHARED plugin.cpp
        ../../include/illixr/data_format.hpp
        ../../include/illixr/phonebook.hpp
        ../../include/illixr/switchboard.hpp
        ../../include/illixr/threadloop.hpp
        ../../include/illixr/vk/display_provider.hpp
        ../../include/illixr/vk/vulkan_utils.hpp
        ../../include/illixr/serializable_data.cpp
)

set_target_properties(${PLUGIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})

target_link_libraries(${PLUGIN_NAME} spdlog::spdlog ${Vulkan_LIBRARIES})
target_include_directories(${PLUGIN_NAME} PUBLIC ${Vulkan_INCLUDE_DIRS} ${ILLIXR_SOURCE_DIR}/include)

target_link_libraries(${PLUGIN_NAME} PkgConfig::LIBAV)
target_include_directories(${PLUGIN_NAME} PUBLIC ${LIBAV_INCLUDE_DIRS})

target_link_libraries(${PLUGIN_NAME} CUDA::cudart CUDA::nppif CUDA::nppial CUDA::nppicc CUDA::nppidei CUDA::nppig CUDA::nppim CUDA::nppist CUDA::nppisu CUDA::nppitc CUDA::npps CUDA::nvToolsExt)
target_include_directories(${PLUGIN_NAME} PUBLIC ${CUDAToolkit_INCLUDE_DIRS})

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
