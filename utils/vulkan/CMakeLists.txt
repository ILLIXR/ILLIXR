add_library(illixr_vulkan_utils STATIC
            vulkan_utils.cpp
            vulkan_utils.hpp
)

target_include_directories(illixr_vulkan_utils PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(illixr_vulkan_utils PUBLIC spdlog::spdlog)
