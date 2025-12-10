set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_NAME plugin.offload_rendering_nvenc.tx${ILLIXR_BUILD_SUFFIX})

find_package(Protobuf REQUIRED)
find_package(CUDAToolkit REQUIRED)

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/../proto/rendered_frame.proto)

add_library(${PLUGIN_NAME} SHARED
            plugin.hpp
            plugin.cpp
            nvenc_encoder.hpp
            nvenc_encoder.cpp
            ${PROTO_SRCS}
            ${PROTO_HDRS}
)

target_include_directories(${PLUGIN_NAME} PUBLIC
                           ${CMAKE_CURRENT_BINARY_DIR}
                           ${OpenCV_INCLUDE_DIRS}
                           C:/Users/fried/Downloads/Video_Codec_SDK_12.2.72/Video_Codec_SDK_12.2.72/Interface
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      CUDA::cudart
                      CUDA::cuda_driver
                      ${OpenCV_LIBS}
                      protobuf::libprotobuf
                      spdlog::spdlog
                      Boost::serialization
)

if(WIN32 OR MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
    target_compile_options(${PLUGIN_NAME} PRIVATE /EHsc)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
