set(PLUGIN_NAME test_oxr${ILLIXR_BUILD_SUFFIX})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PLUGIN_NAME} SHARED
            plugin.cpp
            plugin.hpp
            ${CMAKE_SOURCE_DIR}/include/illixr/data_format/openxr_payloads.h
)

target_include_directories(${PLUGIN_NAME} PUBLIC ${ILLIXR_SOURCE_DIR}/include
                           ${XRT_SOURCE_DIR}/src/xrt
                           ${XRT_SOURCE_DIR}/src/xrt/include
)

target_link_libraries(${PLUGIN_NAME} PUBLIC
                      Boost::serialization
                      Eigen3::Eigen
                      spdlog::spdlog
)

if (WIN32 OR MSVC)
    target_link_libraries(${PLUGIN_NAME} PUBLIC ws2_32)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE BUILDING_LIBRARY)
    target_compile_options(${PLUGIN_NAME} PRIVATE /EHsc)
endif()

install(TARGETS ${PLUGIN_NAME} DESTINATION lib)
